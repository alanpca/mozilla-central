# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import datetime
from errors import *
from mozdevice import devicemanagerADB
from mozprocess import ProcessHandlerMixin
import multiprocessing
import os
import re
import platform
import shutil
import socket
import subprocess
from telnetlib import Telnet
import tempfile
import time

from emulator_battery import EmulatorBattery
from emulator_geo import EmulatorGeo


class LogcatProc(ProcessHandlerMixin):
    """Process handler for logcat which saves all output to a logfile.
    """

    def __init__(self, logfile, cmd, **kwargs):
        self.logfile = logfile
        kwargs.setdefault('processOutputLine', []).append(self.log_output)
        ProcessHandlerMixin.__init__(self, cmd, **kwargs)

    def log_output(self, line):
        f = open(self.logfile, 'a')
        f.write(line + "\n")
        f.flush()


class Emulator(object):

    deviceRe = re.compile(r"^emulator-(\d+)(\s*)(.*)$")

    def __init__(self, homedir=None, noWindow=False, logcat_dir=None,
                 arch="x86", emulatorBinary=None, res='480x800', sdcard=None,
                 userdata=None):
        self.port = None
        self.dm = None
        self._emulator_launched = False
        self.proc = None
        self.marionette_port = None
        self.telnet = None
        self._tmp_sdcard = None
        self._tmp_userdata = None
        self._adb_started = False
        self.logcat_dir = logcat_dir
        self.logcat_proc = None
        self.arch = arch
        self.binary = emulatorBinary
        self.res = res
        self.battery = EmulatorBattery(self)
        self.geo = EmulatorGeo(self)
        self.homedir = homedir
        self.sdcard = sdcard
        self.noWindow = noWindow
        if self.homedir is not None:
            self.homedir = os.path.expanduser(homedir)
        self.dataImg = userdata
        self.copy_userdata = self.dataImg is None

    def _check_for_b2g(self):
        if self.homedir is None:
            self.homedir = os.getenv('B2G_HOME')
        if self.homedir is None:
            raise Exception('Must define B2G_HOME or pass the homedir parameter')
        self._check_file(self.homedir)

        oldstyle_homedir = os.path.join(self.homedir, 'glue', 'gonk-ics')
        if os.access(oldstyle_homedir, os.F_OK):
            self.homedir = oldstyle_homedir

        if self.arch not in ("x86", "arm"):
            raise Exception("Emulator architecture must be one of x86, arm, got: %s" %
                            self.arch)

        host_dir = "linux-x86"
        if platform.system() == "Darwin":
            host_dir = "darwin-x86"

        host_bin_dir = os.path.join("out", "host", host_dir, "bin")

        if self.arch == "x86":
            binary = os.path.join(host_bin_dir, "emulator-x86")
            kernel = "prebuilts/qemu-kernel/x86/kernel-qemu"
            sysdir = "out/target/product/generic_x86"
            self.tail_args = []
        else:
            binary = os.path.join(host_bin_dir, "emulator")
            kernel = "prebuilts/qemu-kernel/arm/kernel-qemu-armv7"
            sysdir = "out/target/product/generic"
            self.tail_args = ["-cpu", "cortex-a8"]

        self._check_for_adb()
        if(self.sdcard):
            self.mksdcard = os.path.join(self.homedir, host_bin_dir, "mksdcard")
            self.create_sdcard(self.sdcard)

        if not self.binary:
            self.binary = os.path.join(self.homedir, binary)

        self._check_file(self.binary)

        self.kernelImg = os.path.join(self.homedir, kernel)
        self._check_file(self.kernelImg)

        self.sysDir = os.path.join(self.homedir, sysdir)
        self._check_file(self.sysDir)

        if not self.dataImg:
            self.dataImg = os.path.join(self.sysDir, 'userdata.img')
        self._check_file(self.dataImg)

    def __del__(self):
        if self.telnet:
            self.telnet.write('exit\n')
            self.telnet.read_all()

    def _check_file(self, filePath):
        if not os.access(filePath, os.F_OK):
            raise Exception(('File not found: %s; did you pass the B2G home '
                             'directory as the homedir parameter, or set '
                             'B2G_HOME correctly?') % filePath)

    @property
    def args(self):
        qemuArgs = [self.binary,
                    '-kernel', self.kernelImg,
                    '-sysdir', self.sysDir,
                    '-data', self.dataImg]
        if self._tmp_sdcard:
            qemuArgs.extend(['-sdcard', self._tmp_sdcard])
        if self.noWindow:
            qemuArgs.append('-no-window')
        qemuArgs.extend(['-memory', '512',
                         '-partition-size', '512',
                         '-verbose',
                         '-skin', self.res,
                         '-gpu', 'on',
                         '-qemu'] + self.tail_args)
        return qemuArgs

    @property
    def is_running(self):
        if self._emulator_launched:
            return self.proc is not None and self.proc.poll() is None
        else:
            return self.port is not None

    def check_for_crash(self):
        """
        Checks if the emulator has crashed or not.  Always returns False if
        we've connected to an already-running emulator, since we can't track
        the emulator's pid in that case.  Otherwise, returns True iff
        self.proc is not None (meaning the emulator hasn't been explicitly
        closed), and self.proc.poll() is also not None (meaning the emulator
        process has terminated).
        """
        if (self._emulator_launched and self.proc is not None
                                    and self.proc.poll() is not None):
            return True
        return False

    def create_sdcard(self, sdcard):
        self._tmp_sdcard = tempfile.mktemp(prefix='sdcard')
        sdargs = [self.mksdcard, "-l", "mySdCard", sdcard, self._tmp_sdcard]
        sd = subprocess.Popen(sdargs, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        retcode = sd.wait()
        if retcode:
            raise Exception('unable to create sdcard : exit code %d: %s'
                            % (retcode, sd.stdout.read()))
        return None

    def _check_for_adb(self):
        host_dir = "linux-x86"
        if platform.system() == "Darwin":
            host_dir = "darwin-x86"
        adb = subprocess.Popen(['which', 'adb'],
                                stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)
        if adb.wait() == 0:
            self.adb = adb.stdout.read().strip()  # remove trailing newline
            return
        adb_paths = [os.path.join(self.homedir, 'glue', 'gonk', 'out', 'host',
                                  host_dir, 'bin', 'adb'),
                     os.path.join(self.homedir, 'out', 'host', host_dir,
                                  'bin', 'adb'),
                     os.path.join(self.homedir, 'bin', 'adb')]
        for option in adb_paths:
            if os.path.exists(option):
                self.adb = option
                return
        raise Exception('adb not found!')

    def _run_adb(self, args):
        args.insert(0, self.adb)
        if self.port:
            args.insert(1, '-s')
            args.insert(2, 'emulator-%d' % self.port)
        adb = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        retcode = adb.wait()
        if retcode:
            raise Exception('adb terminated with exit code %d: %s'
                            % (retcode, adb.stdout.read()))
        return adb.stdout.read()

    def _get_telnet_response(self, command=None):
        output = []
        assert(self.telnet)
        if command is not None:
            self.telnet.write('%s\n' % command)
        while True:
            line = self.telnet.read_until('\n')
            output.append(line.rstrip())
            if line.startswith('OK'):
                return output
            elif line.startswith('KO:'):
                raise Exception('bad telnet response: %s' % line)

    def _run_telnet(self, command):
        if not self.telnet:
            self.telnet = Telnet('localhost', self.port)
            self._get_telnet_response()
        return self._get_telnet_response(command)

    def close(self):
        if self.is_running and self._emulator_launched:
            self.proc.terminate()
            self.proc.wait()
        if self._adb_started:
            self._run_adb(['kill-server'])
            self._adb_started = False
        if self.proc:
            retcode = self.proc.poll()
            self.proc = None
            if self._tmp_userdata:
                os.remove(self._tmp_userdata)
                self._tmp_userdata = None
            if self._tmp_sdcard:
                os.remove(self._tmp_sdcard)
                self._tmp_sdcard = None
            return retcode
        if self.logcat_proc:
            self.logcat_proc.kill()
        return 0

    def _get_adb_devices(self):
        offline = set()
        online = set()
        output = self._run_adb(['devices'])
        for line in output.split('\n'):
            m = self.deviceRe.match(line)
            if m:
                if m.group(3) == 'offline':
                    offline.add(m.group(1))
                else:
                    online.add(m.group(1))
        return (online, offline)

    def restart(self, port):
        if not self._emulator_launched:
            return
        self.close()
        self.start()
        return self.setup_port_forwarding(port)

    def start_adb(self):
        result = self._run_adb(['start-server'])
        # We keep track of whether we've started adb or not, so we know
        # if we need to kill it.
        if 'daemon started successfully' in result:
            self._adb_started = True
        else:
            self._adb_started = False

    def wait_for_system_message(self, marionette):
        marionette.start_session()
        marionette.set_context(marionette.CONTEXT_CHROME)
        marionette.set_script_timeout(45000)
        # Telephony API's won't be available immediately upon emulator
        # boot; we have to wait for the syste-message-listener-ready
        # message before we'll be able to use them successfully.  See
        # bug 792647.
        print 'waiting for system-message-listener-ready...'
        try:
            marionette.execute_async_script("""
waitFor(
    function() { marionetteScriptFinished(true); },
    function() { return isSystemMessageListenerReady(); }
);
            """)
        except ScriptTimeoutException:
            print 'timed out'
            # We silently ignore the timeout if it occurs, since
            # isSystemMessageListenerReady() isn't available on
            # older emulators.  45s *should* be enough of a delay
            # to allow telephony API's to work.
            pass
        print 'done'
        marionette.set_context(marionette.CONTEXT_CONTENT)
        marionette.delete_session()


    def connect(self):
        self._check_for_adb()
        self.start_adb()

        online, offline = self._get_adb_devices()
        now = datetime.datetime.now()
        while online == set([]):
            time.sleep(1)
            if datetime.datetime.now() - now > datetime.timedelta(seconds=60):
                raise Exception('timed out waiting for emulator to be available')
            online, offline = self._get_adb_devices()
        self.port = int(list(online)[0])

        self.dm = devicemanagerADB.DeviceManagerADB(adbPath=self.adb,
                                                    deviceSerial='emulator-%d' % self.port)

    def start(self):
        self._check_for_b2g()
        self.start_adb()

        qemu_args = self.args[:]
        if self.copy_userdata:
            # Make a copy of the userdata.img for this instance of the emulator
            # to use.
            self._tmp_userdata = tempfile.mktemp(prefix='marionette')
            shutil.copyfile(self.dataImg, self._tmp_userdata)
            qemu_args[qemu_args.index('-data') + 1] = self._tmp_userdata

        original_online, original_offline = self._get_adb_devices()

        self.proc = subprocess.Popen(qemu_args,
                                     stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE)

        online, offline = self._get_adb_devices()
        now = datetime.datetime.now()
        while online - original_online == set([]):
            time.sleep(1)
            if datetime.datetime.now() - now > datetime.timedelta(seconds=60):
                raise Exception('timed out waiting for emulator to start')
            online, offline = self._get_adb_devices()
        self.port = int(list(online - original_online)[0])
        self._emulator_launched = True

        self.dm = devicemanagerADB.DeviceManagerADB(adbPath=self.adb,
                                                    deviceSerial='emulator-%d' % self.port)

        # bug 802877
        time.sleep(10)
        self.geo.set_default_location()

        if self.logcat_dir:
            self.save_logcat()

        # setup DNS fix for networking
        self._run_adb(['shell', 'setprop', 'net.dns1', '10.0.2.3'])

    def _save_logcat_proc(self, filename, cmd):
        self.logcat_proc = LogcatProc(filename, cmd)
        self.logcat_proc.run()
        self.logcat_proc.processOutput()
        self.logcat_proc.waitForFinish()
        self.logcat_proc = None

    def install_gecko(self, gecko_path, marionette):
        """
        Install gecko into the emulator using adb push.  Restart b2g after the
        installation.
        """
        print 'installing gecko binaries...'
        # need to remount so we can write to /system/b2g
        self._run_adb(['remount'])
        # See bug 800102.  We use this particular method of installing
        # gecko in order to avoid an adb bug in which adb will sometimes
        # hang indefinitely while copying large files to the system
        # partition.
        for root, dirs, files in os.walk(gecko_path):
            for filename in files:
                data_local_file = os.path.join('/data/local', filename)
                print 'pushing', data_local_file
                self.dm.pushFile(os.path.join(root, filename), data_local_file)
        self.dm.shellCheckOutput(['stop', 'b2g'])
        for root, dirs, files in os.walk(gecko_path):
            for filename in files:
                data_local_file = os.path.join('/data/local', filename)
                rel_file = os.path.relpath(os.path.join(root, filename), gecko_path)
                system_file = os.path.join('/system/b2g', rel_file)
                print 'copying', data_local_file, 'to', system_file
                self.dm.shellCheckOutput(['dd', 'if=%s' % data_local_file,
                                          'of=%s' % system_file])
        print 'restarting B2G'
        self.dm.shellCheckOutput(['start', 'b2g'])
        self.wait_for_port()
        self.wait_for_system_message(marionette)

    def rotate_log(self, srclog, index=1):
        """ Rotate a logfile, by recursively rotating logs further in the sequence,
            deleting the last file if necessary.
        """
        destlog = os.path.join(self.logcat_dir, 'emulator-%d.%d.log' % (self.port, index))
        if os.access(destlog, os.F_OK):
            if index == 3:
                os.remove(destlog)
            else:
                self.rotate_log(destlog, index+1)
        shutil.move(srclog, destlog)

    def save_logcat(self):
        """ Save the output of logcat to a file.
        """
        filename = os.path.join(self.logcat_dir, "emulator-%d.log" % self.port)
        if os.access(filename, os.F_OK):
            self.rotate_log(filename)
        cmd = [self.adb, '-s', 'emulator-%d' % self.port, 'logcat']

        # We do this in a separate process because we call mozprocess's
        # waitForFinish method to process logcat's output, and this method
        # blocks.
        proc = multiprocessing.Process(target=self._save_logcat_proc, args=(filename, cmd))
        proc.daemon = True
        proc.start()

    def setup_port_forwarding(self, remote_port):
        """ Set up TCP port forwarding to the specified port on the device,
            using any availble local port, and return the local port.
        """

        import socket
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.bind(("",0))
        local_port = s.getsockname()[1]
        s.close()

        output = self._run_adb(['forward',
                                'tcp:%d' % local_port,
                                'tcp:%d' % remote_port])

        self.marionette_port = local_port

        return local_port

    def wait_for_port(self, timeout=300):
        assert(self.marionette_port)
        starttime = datetime.datetime.now()
        while datetime.datetime.now() - starttime < datetime.timedelta(seconds=timeout):
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.connect(('localhost', self.marionette_port))
                data = sock.recv(16)
                sock.close()
                if '"from"' in data:
                    return True
            except:
                import traceback
                print traceback.format_exc()
            time.sleep(1)
        return False
