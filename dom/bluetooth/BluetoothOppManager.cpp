/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/basictypes.h"
#include "BluetoothOppManager.h"

#include "BluetoothReplyRunnable.h"
#include "BluetoothService.h"
#include "BluetoothServiceUuid.h"
#include "BluetoothUtils.h"
#include "ObexBase.h"

#include "mozilla/dom/bluetooth/BluetoothTypes.h"
#include "mozilla/RefPtr.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPtr.h"
#include "nsIObserver.h"
#include "nsIObserverService.h"
#include "nsIDOMFile.h"
#include "nsIFile.h"
#include "nsIInputStream.h"
#include "nsIOutputStream.h"
#include "nsNetUtil.h"

#define TARGET_FOLDER "/sdcard/downloads/bluetooth/"

USING_BLUETOOTH_NAMESPACE
using namespace mozilla;
using namespace mozilla::ipc;

class BluetoothOppManagerObserver : public nsIObserver
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  BluetoothOppManagerObserver()
  {
  }

  bool Init()
  {
    nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
    if (NS_FAILED(obs->AddObserver(this,
                                   NS_XPCOM_SHUTDOWN_OBSERVER_ID,
                                   false))) {
      NS_WARNING("Failed to add shutdown observer!");
      return false;
    }

    return true;
  }

  bool Shutdown()
  {
    nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
    if (!obs ||
        (NS_FAILED(obs->RemoveObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID)))) {
      NS_WARNING("Can't unregister observers, or already unregistered!");
      return false;
    }
    return true;
  }

  ~BluetoothOppManagerObserver()
  {
    Shutdown();
  }
};

namespace {
// Sending system message "bluetooth-opp-update-progress" every 50kb
static const uint32_t kUpdateProgressBase = 50 * 1024;
StaticRefPtr<BluetoothOppManager> sInstance;
StaticRefPtr<BluetoothOppManagerObserver> sOppObserver;

/*
 * FIXME / Bug 806749
 *
 * Currently Bluetooth*Manager inherits mozilla::ipc::UnixSocketConsumer,
 * which means that each Bluetooth*Manager can handle only one socket
 * connection at a time. We need to support concurrent multiple socket
 * connections, and then we will be able to have multiple file transferring
 * sessions at a time.
 */
static uint32_t sSentFileLength = 0;
static nsString sFileName;
static uint32_t sFileLength = 0;
static nsString sContentType;
static bool sInShutdown = false;
}

NS_IMETHODIMP
BluetoothOppManagerObserver::Observe(nsISupports* aSubject,
                                     const char* aTopic,
                                     const PRUnichar* aData)
{
  MOZ_ASSERT(sInstance);

  if (!strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID)) {
    return sInstance->HandleShutdown();
  }

  MOZ_ASSERT(false, "BluetoothOppManager got unexpected topic!");
  return NS_ERROR_UNEXPECTED;
}

class ReadFileTask : public nsRunnable
{
public:
  ReadFileTask(nsIInputStream* aInputStream) : mInputStream(aInputStream)
  {
    MOZ_ASSERT(NS_IsMainThread());
  }

  NS_IMETHOD Run()
  {
    MOZ_ASSERT(!NS_IsMainThread());

    /*
     * 255 is the Minimum OBEX Packet Length (See section 3.3.1.4,
     * IrOBEX ver 1.2)
     */
    char buf[255];
    uint32_t numRead;

    // function inputstream->Read() only works on non-main thread
    nsresult rv = mInputStream->Read(buf, sizeof(buf), &numRead);
    if (NS_FAILED(rv)) {
      // Needs error handling here
      return NS_ERROR_FAILURE;
    }

    if (numRead > 0) {
      if (sSentFileLength + numRead >= sFileLength) {
        sInstance->SendPutRequest((uint8_t*)buf, numRead, true);
      } else {
        sInstance->SendPutRequest((uint8_t*)buf, numRead, false);
      }

      sSentFileLength += numRead;
    }

    return NS_OK;
  };

private:
  nsCOMPtr<nsIInputStream> mInputStream;
};

BluetoothOppManager::BluetoothOppManager() : mConnected(false)
                                           , mConnectionId(1)
                                           , mLastCommand(0)
                                           , mRemoteObexVersion(0)
                                           , mRemoteConnectionFlags(0)
                                           , mRemoteMaxPacketLength(0)
                                           , mAbortFlag(false)
                                           , mPacketLeftLength(0)
                                           , mReceiving(false)
                                           , mPutFinal(false)
                                           , mWaitingForConfirmationFlag(false)
{
  mConnectedDeviceAddress.AssignLiteral("00:00:00:00:00:00");
  mSocketStatus = GetConnectionStatus();
}

BluetoothOppManager::~BluetoothOppManager()
{
}

//static
BluetoothOppManager*
BluetoothOppManager::Get()
{
  MOZ_ASSERT(NS_IsMainThread());

  if (sInstance == nullptr) {
    sInstance = new BluetoothOppManager();
  }

  return sInstance;
}

bool
BluetoothOppManager::Connect(const nsAString& aDeviceObjectPath,
                             BluetoothReplyRunnable* aRunnable)
{
  MOZ_ASSERT(NS_IsMainThread());

  BluetoothService* bs = BluetoothService::Get();
  if (!bs) {
    NS_WARNING("BluetoothService not available!");
    return false;
  }

  nsString serviceUuidStr =
    NS_ConvertUTF8toUTF16(BluetoothServiceUuidStr::ObjectPush);

  nsRefPtr<BluetoothReplyRunnable> runnable = aRunnable;

  nsresult rv = bs->GetSocketViaService(aDeviceObjectPath,
                                        serviceUuidStr,
                                        BluetoothSocketType::RFCOMM,
                                        true,
                                        true,
                                        this,
                                        runnable);

  runnable.forget();
  return NS_FAILED(rv) ? false : true;
}

void
BluetoothOppManager::Disconnect()
{
  CloseSocket();
}

nsresult
BluetoothOppManager::HandleShutdown()
{
  MOZ_ASSERT(NS_IsMainThread());
  sInShutdown = true;
  CloseSocket();
  sInstance = nullptr;
  return NS_OK;
}

bool
BluetoothOppManager::Listen()
{
  MOZ_ASSERT(NS_IsMainThread());

  CloseSocket();

  BluetoothService* bs = BluetoothService::Get();
  if (!bs) {
    NS_WARNING("BluetoothService not available!");
    return false;
  }

  nsresult rv = bs->ListenSocketViaService(BluetoothReservedChannels::OPUSH,
                                           BluetoothSocketType::RFCOMM,
                                           true,
                                           true,
                                           this);
  mSocketStatus = GetConnectionStatus();

  return NS_FAILED(rv) ? false : true;
}

bool
BluetoothOppManager::SendFile(BlobParent* aActor)
{
  if (mBlob) {
    // Means there's a sending process. Reply error.
    return false;
  }

  /*
   * Process of sending a file:
   *  - Keep blob because OPP connection has not been established yet.
   *  - Create an OPP connection by SendConnectRequest()
   *  - After receiving the response, start to read file and send.
   */
  mBlob = aActor->GetBlob();

  SendConnectRequest();

  return true;
}

bool
BluetoothOppManager::StopSendingFile()
{
  mAbortFlag = true;

  return true;
}

bool
BluetoothOppManager::ConfirmReceivingFile(bool aConfirm)
{
  if (!mWaitingForConfirmationFlag) {
    NS_WARNING("We are not waiting for a confirmation now.");
    return false;
  }

  NS_ASSERTION(mPacketLeftLength == 0,
               "Should not be in the middle of receiving a PUT packet.");

  mWaitingForConfirmationFlag = false;
  ReplyToPut(mPutFinal, aConfirm);

  if (aConfirm) {
    StartFileTransfer(mConnectedDeviceAddress, true,
                      sFileName, sFileLength, sContentType);
  } else {
    DeleteReceivedFile();
  }

  if (mPutFinal || !aConfirm) {
    mReceiving = false;
    FileTransferComplete(mConnectedDeviceAddress, aConfirm, true, sFileName,
                         sSentFileLength, sContentType);
  }

  return true;
}

void
BluetoothOppManager::AfterOppConnected()
{
  MOZ_ASSERT(NS_IsMainThread());

  mConnected = true;
  mUpdateProgressCounter = 1;
  sSentFileLength = 0;
  mAbortFlag = false;
}

void
BluetoothOppManager::AfterOppDisconnected()
{
  MOZ_ASSERT(NS_IsMainThread());

  mConnected = false;
  mReceiving = false;
  mLastCommand = 0;
  mBlob = nullptr;

  if (mInputStream) {
    mInputStream->Close();
    mInputStream = nullptr;
  }

  if (mOutputStream) {
    mOutputStream->Close();
    mOutputStream = nullptr;
  }

  if (mReadFileThread) {
    mReadFileThread->Shutdown();
    mReadFileThread = nullptr;
  }

  mConnectedDeviceAddress.AssignLiteral("00:00:00:00:00:00");
}

void
BluetoothOppManager::DeleteReceivedFile()
{
  nsString path;
  path.AssignLiteral(TARGET_FOLDER);
  path += sFileName;

  nsCOMPtr<nsIFile> f;
  nsresult rv = NS_NewLocalFile(path + sFileName, false, getter_AddRefs(f));
  if (NS_FAILED(rv)) {
    NS_WARNING("Couldn't find received file, nothing to delete.");
    return;
  }

  if (mOutputStream) {
    mOutputStream->Close();
    mOutputStream = nullptr;
  }

  f->Remove(false);
}

// Virtual function of class SocketConsumer
void
BluetoothOppManager::ReceiveSocketData(UnixSocketRawData* aMessage)
{
  uint8_t opCode;
  int packetLength;
  int receivedLength = aMessage->mSize;

  if (mPacketLeftLength > 0) {
    opCode = mPutFinal ? ObexRequestCode::PutFinal : ObexRequestCode::Put;
    packetLength = mPacketLeftLength;
  } else {
    opCode = aMessage->mData[0];
    packetLength = (((int)aMessage->mData[1]) << 8) | aMessage->mData[2];
  }

  if (mLastCommand == ObexRequestCode::Connect) {
    if (opCode == ObexResponseCode::Success) {
      AfterOppConnected();

      // Keep remote information
      mRemoteObexVersion = aMessage->mData[3];
      mRemoteConnectionFlags = aMessage->mData[4];
      mRemoteMaxPacketLength =
        (((int)(aMessage->mData[5]) << 8) | aMessage->mData[6]);

      if (mBlob) {
        /*
         * Before sending content, we have to send a header including
         * information such as file name, file length and content type.
         */
        nsresult rv;
        nsCOMPtr<nsIDOMFile> file = do_QueryInterface(mBlob);
        if (file) {
          rv = file->GetName(sFileName);
        }

        if (!file || sFileName.IsEmpty()) {
          sFileName.AssignLiteral("Unknown");
        }

        rv = mBlob->GetType(sContentType);
        if (NS_FAILED(rv)) {
          NS_WARNING("Can't get content type");
          return;
        }

        uint64_t fileLength;
        rv = mBlob->GetSize(&fileLength);
        if (NS_FAILED(rv)) {
          NS_WARNING("Can't get file size");
          return;
        }

        // Currently we keep the size of files which were sent/received via
        // Bluetooth not exceed UINT32_MAX because the Length header in OBEX
        // is only 4-byte long. Although it is possible to transfer a file
        // larger than UINT32_MAX, it needs to parse another OBEX Header
        // and I would like to leave it as a feature.
        if (fileLength <= UINT32_MAX) {
          NS_WARNING("The file size is too large for now");
          SendDisconnectRequest();
          return;
        }

        sFileLength = fileLength;

        if (NS_FAILED(NS_NewThread(getter_AddRefs(mReadFileThread)))) {
          NS_WARNING("Can't create thread");
          SendDisconnectRequest();
          return;
        }

        sInstance->SendPutHeaderRequest(sFileName, sFileLength);
        StartFileTransfer(mConnectedDeviceAddress, false,
                          sFileName, sFileLength, sContentType);
      }
    }
  } else if (mLastCommand == ObexRequestCode::Disconnect) {
    if (opCode != ObexResponseCode::Success) {
      // FIXME: Needs error handling here
      NS_WARNING("[OPP] Disconnect failed");
    }

    AfterOppDisconnected();
  } else if (mLastCommand == ObexRequestCode::Put) {
    if (opCode != ObexResponseCode::Continue) {
      // FIXME: Needs error handling here
      NS_WARNING("[OPP] Put failed");
      return;
    }

    if (mAbortFlag || mReadFileThread) {
      SendAbortRequest();
      return;
    }

    if (kUpdateProgressBase * mUpdateProgressCounter < sSentFileLength) {
      UpdateProgress(mConnectedDeviceAddress, false,
                     sSentFileLength, sFileLength);
      mUpdateProgressCounter = sSentFileLength / kUpdateProgressBase + 1;
    }

    if (mInputStream) {
      nsresult rv = mBlob->GetInternalStream(getter_AddRefs(mInputStream));
      if (NS_FAILED(rv)) {
        NS_WARNING("Can't get internal stream of blob");
        return;
      }
    }

    nsRefPtr<ReadFileTask> task = new ReadFileTask(mInputStream);
    if (NS_FAILED(mReadFileThread->Dispatch(task, NS_DISPATCH_NORMAL))) {
      NS_WARNING("Cannot dispatch ring task!");
    }
  } else if (mLastCommand == ObexRequestCode::PutFinal) {
    if (opCode != ObexResponseCode::Success) {
      // FIXME: Needs error handling here
      NS_WARNING("[OPP] PutFinal failed");
      return;
    }

    FileTransferComplete(mConnectedDeviceAddress, true, false, sFileName,
                         sSentFileLength, sContentType);
    SendDisconnectRequest();
  } else if (mLastCommand == ObexRequestCode::Abort) {
    if (opCode != ObexResponseCode::Success) {
      NS_WARNING("[OPP] Abort failed");
    }

    FileTransferComplete(mConnectedDeviceAddress, false, false, sFileName,
                         sSentFileLength, sContentType);
    SendDisconnectRequest();
  } else {
    // Remote request or unknown mLastCommand
    ObexHeaderSet pktHeaders(opCode);

    if (opCode == ObexRequestCode::Connect) {
      // Section 3.3.1 "Connect", IrOBEX 1.2
      // [opcode:1][length:2][version:1][flags:1][MaxPktSizeWeCanReceive:2]
      // [Headers:var]
      ParseHeadersAndFindBody(&aMessage->mData[7],
                              receivedLength - 7,
                              &pktHeaders);
      ReplyToConnect();
      AfterOppConnected();
    } else if (opCode == ObexRequestCode::Disconnect) {
      // Section 3.3.2 "Disconnect", IrOBEX 1.2
      // [opcode:1][length:2][Headers:var]
      ParseHeadersAndFindBody(&aMessage->mData[3],
                              receivedLength - 3,
                              &pktHeaders);
      ReplyToDisconnect();
      AfterOppDisconnected();
    } else if (opCode == ObexRequestCode::Put ||
               opCode == ObexRequestCode::PutFinal) {
      // Section 3.3.3 "Put", IrOBEX 1.2
      // [opcode:1][length:2][Headers:var]
      int headerStartIndex = 3;

      if (!mReceiving) {
        nsString path;
        path.AssignLiteral(TARGET_FOLDER);

        MOZ_ASSERT(mPacketLeftLength == 0);
        ParseHeadersAndFindBody(&aMessage->mData[headerStartIndex],
                                receivedLength - headerStartIndex,
                                &pktHeaders);

        pktHeaders.GetName(sFileName);
        pktHeaders.GetContentType(sContentType);
        pktHeaders.GetLength(&sFileLength);

        nsCOMPtr<nsIFile> f;
        nsresult rv;
        rv = NS_NewLocalFile(path + sFileName, false, getter_AddRefs(f));
        if (NS_FAILED(rv)) {
          NS_WARNING("Couldn't new a local file");
        }

        rv = f->CreateUnique(nsIFile::NORMAL_FILE_TYPE, 00644);
        if (NS_FAILED(rv)) {
          NS_WARNING("Couldn't create the file");
        }

        /*
         * The function CreateUnique() may create a file with a different file
         * name from the original sFileName. Therefore we have to retrieve
         * the file name again.
         */
        f->GetLeafName(sFileName);

        NS_NewLocalFileOutputStream(getter_AddRefs(mOutputStream), f);
        if (!mOutputStream) {
          NS_WARNING("Couldn't new an output stream");
        }

        mReceiving = true;
        mWaitingForConfirmationFlag = true;
      }

      /*
       * A PUT request from remote devices may be divided into multiple parts.
       * In other words, one request may need to be received multiple times,
       * so here we keep a variable mPacketLeftLength to indicate if current
       * PUT request is done.
       */
      mPutFinal = (opCode == ObexRequestCode::PutFinal);

      uint32_t wrote = 0;
      if (mPacketLeftLength == 0) {
        NS_ASSERTION(packetLength >= receivedLength,
                     "Invalid packet length");
        mPacketLeftLength = packetLength - receivedLength;

        int headerBodyOffset =
          ParseHeadersAndFindBody(&aMessage->mData[headerStartIndex],
                                  receivedLength - headerStartIndex,
                                  &pktHeaders);

        if (headerBodyOffset != -1) {
          /*
           * Adding by 3 is because the format of a header is like:
           *     [HeaderId:1 (BODY)][HeaderLength:2][Data:n]
           * and headerStartIndex + headerBodyOffset points to HeaderId,
           * so adding 3 is to point to the beginning of data.
           *
           */
          int fileBodyIndex = headerStartIndex + headerBodyOffset + 3;

          mOutputStream->Write((char*)&aMessage->mData[fileBodyIndex],
                               receivedLength - fileBodyIndex, &wrote);
          NS_ASSERTION(receivedLength - fileBodyIndex == wrote,
                       "Writing to the file failed");
        }
      } else {
        NS_ASSERTION(mPacketLeftLength >= receivedLength,
                     "Invalid packet length");
        mPacketLeftLength -= receivedLength;

        mOutputStream->Write((char*)&aMessage->mData[0],
                             receivedLength,
                             &wrote);
        NS_ASSERTION(receivedLength == wrote, "Writing to the file failed");
      }

      sSentFileLength += wrote;
      if (sSentFileLength > kUpdateProgressBase * mUpdateProgressCounter &&
          !mWaitingForConfirmationFlag) {
        UpdateProgress(mConnectedDeviceAddress, true,
                       sSentFileLength, sFileLength);
        mUpdateProgressCounter = sSentFileLength / kUpdateProgressBase + 1;
      }

      if (mPacketLeftLength == 0) {
        if (mWaitingForConfirmationFlag) {
          ReceivingFileConfirmation(mConnectedDeviceAddress, sFileName,
                                    sFileLength, sContentType);
        } else {
          ReplyToPut(mPutFinal, mAbortFlag ? false : true);

          if (mAbortFlag) {
            mReceiving = false;
            FileTransferComplete(mConnectedDeviceAddress, false, true,
                                 sFileName, sSentFileLength, sContentType);
            DeleteReceivedFile();
          } else if (mPutFinal) {
            mReceiving = false;
            FileTransferComplete(mConnectedDeviceAddress, true, true,
                                 sFileName, sSentFileLength, sContentType);
          }
        }
      }
    }
  }
}

void
BluetoothOppManager::SendConnectRequest()
{
  if (mConnected) return;

  // Section 3.3.1 "Connect", IrOBEX 1.2
  // [opcode:1][length:2][version:1][flags:1][MaxPktSizeWeCanReceive:2]
  // [Headers:var]
  uint8_t req[255];
  int index = 7;

  req[3] = 0x10; // version=1.0
  req[4] = 0x00; // flag=0x00
  req[5] = BluetoothOppManager::MAX_PACKET_LENGTH >> 8;
  req[6] = (uint8_t)BluetoothOppManager::MAX_PACKET_LENGTH;

  index += AppendHeaderConnectionId(&req[index], mConnectionId);
  SetObexPacketInfo(req, ObexRequestCode::Connect, index);
  mLastCommand = ObexRequestCode::Connect;

  UnixSocketRawData* s = new UnixSocketRawData(index);
  memcpy(s->mData, req, s->mSize);
  SendSocketData(s);
}

void
BluetoothOppManager::SendPutHeaderRequest(const nsAString& aFileName,
                                          int aFileSize)
{
  uint8_t* req = new uint8_t[mRemoteMaxPacketLength];

  int len = aFileName.Length();
  uint8_t* fileName = new uint8_t[(len + 1) * 2];
  const PRUnichar* fileNamePtr = aFileName.BeginReading();

  for (int i = 0; i < len; i++) {
    fileName[i * 2] = (uint8_t)(fileNamePtr[i] >> 8);
    fileName[i * 2 + 1] = (uint8_t)fileNamePtr[i];
  }

  fileName[len * 2] = 0x00;
  fileName[len * 2 + 1] = 0x00;

  int index = 3;
  index += AppendHeaderConnectionId(&req[index], mConnectionId);
  index += AppendHeaderName(&req[index], (char*)fileName, (len + 1) * 2);
  index += AppendHeaderLength(&req[index], aFileSize);

  SetObexPacketInfo(req, ObexRequestCode::Put, index);
  mLastCommand = ObexRequestCode::Put;

  UnixSocketRawData* s = new UnixSocketRawData(index);
  memcpy(s->mData, req, s->mSize);
  SendSocketData(s);

  delete [] fileName;
  delete [] req;
}

void
BluetoothOppManager::SendPutRequest(uint8_t* aFileBody,
                                    int aFileBodyLength,
                                    bool aFinal)
{
  int index = 3;
  int packetLeftSpace = mRemoteMaxPacketLength - index - 3;

  if (!mConnected) return;
  if (aFileBodyLength > packetLeftSpace) {
    NS_WARNING("Not allowed such a small MaxPacketLength value");
    return;
  }

  // Section 3.3.3 "Put", IrOBEX 1.2
  // [opcode:1][length:2][Headers:var]
  uint8_t* req = new uint8_t[mRemoteMaxPacketLength];

  index += AppendHeaderBody(&req[index], aFileBody, aFileBodyLength);

  if (aFinal) {
    SetObexPacketInfo(req, ObexRequestCode::PutFinal, index);
    mLastCommand = ObexRequestCode::PutFinal;
  } else {
    SetObexPacketInfo(req, ObexRequestCode::Put, index);
    mLastCommand = ObexRequestCode::Put;
  }

  UnixSocketRawData* s = new UnixSocketRawData(index);
  memcpy(s->mData, req, s->mSize);
  SendSocketData(s);

  delete [] req;
}

void
BluetoothOppManager::SendDisconnectRequest()
{
  // Section 3.3.2 "Disconnect", IrOBEX 1.2
  // [opcode:1][length:2][Headers:var]
  uint8_t req[255];
  int index = 3;

  SetObexPacketInfo(req, ObexRequestCode::Disconnect, index);
  mLastCommand = ObexRequestCode::Disconnect;

  UnixSocketRawData* s = new UnixSocketRawData(index);
  memcpy(s->mData, req, s->mSize);
  SendSocketData(s);
}

void
BluetoothOppManager::SendAbortRequest()
{
  // Section 3.3.5 "Abort", IrOBEX 1.2
  // [opcode:1][length:2][Headers:var]
  uint8_t req[255];
  int index = 3;

  SetObexPacketInfo(req, ObexRequestCode::Abort, index);
  mLastCommand = ObexRequestCode::Abort;

  UnixSocketRawData* s = new UnixSocketRawData(index);
  memcpy(s->mData, req, s->mSize);
  SendSocketData(s);
}

void
BluetoothOppManager::ReplyToConnect()
{
  if (mConnected) return;
  mConnected = true;

  // Section 3.3.1 "Connect", IrOBEX 1.2
  // [opcode:1][length:2][version:1][flags:1][MaxPktSizeWeCanReceive:2]
  // [Headers:var]
  uint8_t req[255];
  int index = 7;

  req[3] = 0x10; // version=1.0
  req[4] = 0x00; // flag=0x00
  req[5] = BluetoothOppManager::MAX_PACKET_LENGTH >> 8;
  req[6] = (uint8_t)BluetoothOppManager::MAX_PACKET_LENGTH;

  SetObexPacketInfo(req, ObexResponseCode::Success, index);

  UnixSocketRawData* s = new UnixSocketRawData(index);
  memcpy(s->mData, req, s->mSize);
  SendSocketData(s);
}

void
BluetoothOppManager::ReplyToDisconnect()
{
  if (!mConnected) return;
  mConnected = false;

  // Section 3.3.2 "Disconnect", IrOBEX 1.2
  // [opcode:1][length:2][Headers:var]
  uint8_t req[255];
  int index = 3;

  SetObexPacketInfo(req, ObexResponseCode::Success, index);

  UnixSocketRawData* s = new UnixSocketRawData(index);
  memcpy(s->mData, req, s->mSize);
  SendSocketData(s);
}

void
BluetoothOppManager::ReplyToPut(bool aFinal, bool aContinue)
{
  if (!mConnected) return;

  // Section 3.3.2 "Disconnect", IrOBEX 1.2
  // [opcode:1][length:2][Headers:var]
  uint8_t req[255];
  int index = 3;

  if (aContinue) {
    if (aFinal) {
      SetObexPacketInfo(req, ObexResponseCode::Success, index);
    } else {
      SetObexPacketInfo(req, ObexResponseCode::Continue, index);
    }
  } else {
    if (aFinal) {
      SetObexPacketInfo(req, ObexResponseCode::Unauthorized, index);
    } else {
      SetObexPacketInfo(req,
                        ObexResponseCode::Unauthorized & (~FINAL_BIT),
                        index);
    }
  }

  UnixSocketRawData* s = new UnixSocketRawData(index);
  memcpy(s->mData, req, s->mSize);
  SendSocketData(s);
}

void
BluetoothOppManager::FileTransferComplete(const nsString& aDeviceAddress,
                                          bool aSuccess,
                                          bool aReceived,
                                          const nsString& aFileName,
                                          uint32_t aFileLength,
                                          const nsString& aContentType)
{
  nsString type, name;
  BluetoothValue v;
  InfallibleTArray<BluetoothNamedValue> parameters;
  type.AssignLiteral("bluetooth-opp-transfer-complete");

  name.AssignLiteral("address");
  v = aDeviceAddress;
  parameters.AppendElement(BluetoothNamedValue(name, v));

  name.AssignLiteral("success");
  v = aSuccess;
  parameters.AppendElement(BluetoothNamedValue(name, v));

  name.AssignLiteral("received");
  v = aReceived;
  parameters.AppendElement(BluetoothNamedValue(name, v));

  name.AssignLiteral("fileName");
  v = aFileName;
  parameters.AppendElement(BluetoothNamedValue(name, v));

  name.AssignLiteral("fileLength");
  v = aFileLength;
  parameters.AppendElement(BluetoothNamedValue(name, v));

  name.AssignLiteral("contentType");
  v = aContentType;
  parameters.AppendElement(BluetoothNamedValue(name, v));

  if (!BroadcastSystemMessage(type, parameters)) {
    NS_WARNING("Failed to broadcast [bluetooth-opp-transfer-complete]");
    return;
  }
}

void
BluetoothOppManager::StartFileTransfer(const nsString& aDeviceAddress,
                                       bool aReceived,
                                       const nsString& aFileName,
                                       uint32_t aFileLength,
                                       const nsString& aContentType)
{
  nsString type, name;
  BluetoothValue v;
  InfallibleTArray<BluetoothNamedValue> parameters;
  type.AssignLiteral("bluetooth-opp-transfer-start");

  name.AssignLiteral("address");
  v = aDeviceAddress;
  parameters.AppendElement(BluetoothNamedValue(name, v));

  name.AssignLiteral("received");
  v = aReceived;
  parameters.AppendElement(BluetoothNamedValue(name, v));

  name.AssignLiteral("fileName");
  v = aFileName;
  parameters.AppendElement(BluetoothNamedValue(name, v));

  name.AssignLiteral("fileLength");
  v = aFileLength;
  parameters.AppendElement(BluetoothNamedValue(name, v));

  name.AssignLiteral("contentType");
  v = aContentType;
  parameters.AppendElement(BluetoothNamedValue(name, v));

  if (!BroadcastSystemMessage(type, parameters)) {
    NS_WARNING("Failed to broadcast [bluetooth-opp-transfer-start]");
    return;
  }
}

void
BluetoothOppManager::UpdateProgress(const nsString& aDeviceAddress,
                                    bool aReceived,
                                    uint32_t aProcessedLength,
                                    uint32_t aFileLength)
{
  nsString type, name;
  BluetoothValue v;
  InfallibleTArray<BluetoothNamedValue> parameters;
  type.AssignLiteral("bluetooth-opp-update-progress");

  name.AssignLiteral("address");
  v = aDeviceAddress;
  parameters.AppendElement(BluetoothNamedValue(name, v));

  name.AssignLiteral("received");
  v = aReceived;
  parameters.AppendElement(BluetoothNamedValue(name, v));

  name.AssignLiteral("processedLength");
  v = aProcessedLength;
  parameters.AppendElement(BluetoothNamedValue(name, v));

  name.AssignLiteral("fileLength");
  v = aFileLength;
  parameters.AppendElement(BluetoothNamedValue(name, v));

  if (!BroadcastSystemMessage(type, parameters)) {
    NS_WARNING("Failed to broadcast [bluetooth-opp-update-progress]");
    return;
  }
}

void
BluetoothOppManager::ReceivingFileConfirmation(const nsString& aAddress,
                                               const nsString& aFileName,
                                               uint32_t aFileLength,
                                               const nsString& aContentType)
{
  nsString type, name;
  BluetoothValue v;
  InfallibleTArray<BluetoothNamedValue> parameters;
  type.AssignLiteral("bluetooth-opp-receiving-file-confirmation");

  name.AssignLiteral("address");
  v = aAddress;
  parameters.AppendElement(BluetoothNamedValue(name, v));

  name.AssignLiteral("fileName");
  v = aFileName;
  parameters.AppendElement(BluetoothNamedValue(name, v));

  name.AssignLiteral("fileLength");
  v = aFileLength;
  parameters.AppendElement(BluetoothNamedValue(name, v));

  name.AssignLiteral("contentType");
  v = aContentType;
  parameters.AppendElement(BluetoothNamedValue(name, v));

  if (!BroadcastSystemMessage(type, parameters)) {
    NS_WARNING("Failed to send [bluetooth-opp-receiving-file-confirmation]");
    return;
  }
}

void
BluetoothOppManager::OnConnectSuccess()
{
  // Cache device address since we can't get socket address when a remote
  // device disconnect with us.
  GetSocketAddr(mConnectedDeviceAddress);

  mSocketStatus = GetConnectionStatus();
}

void
BluetoothOppManager::OnConnectError()
{
  CloseSocket();
  mSocketStatus = GetConnectionStatus();
  Listen();
}

void
BluetoothOppManager::OnDisconnect()
{
  // It is valid for a bluetooth device which is transfering file via OPP
  // closing socket without sending OBEX disconnect request first. So we
  // call AfterOppDisconnected here to ensure all variables will be cleaned.
  AfterOppDisconnected();

  if (mSocketStatus == SocketConnectionStatus::SOCKET_CONNECTED) {
    Listen();
  }
}
