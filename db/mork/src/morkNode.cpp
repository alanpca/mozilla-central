/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- 
 * 
 * The contents of this file are subject to the Netscape Public License 
 * Version 1.0 (the "NPL"); you may not use this file except in 
 * compliance with the NPL.  You may obtain a copy of the NPL at 
 * http://www.mozilla.org/NPL/ 
 * 
 * Software distributed under the NPL is distributed on an "AS IS" basis, 
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the NPL 
 * for the specific language governing rights and limitations under the 
 * NPL. 
 * 
 * The Initial Developer of this code under the NPL is Netscape 
 * Communications Corporation.  Portions created by Netscape are 
 * Copyright (C) 1999 Netscape Communications Corporation.  All Rights 
 * Reserved. 
 */

#ifndef _MDB_
#include "mdb.h"
#endif

#ifndef _MORK_
#include "mork.h"
#endif

#ifndef _MORKNODE_
#include "morkNode.h"
#endif

#ifndef _MORKPOOL_
#include "morkPool.h"
#endif

#ifndef _MORKENV_
#include "morkEnv.h"
#endif

#ifndef _MORKHANDLE_
#include "morkHandle.h"
#endif

/*3456789_123456789_123456789_123456789_123456789_123456789_123456789_12345678*/

/* ===== ===== ===== ===== morkUsage ===== ===== ===== ===== */

static morkUsage morkUsage_gHeap; // ensure EnsureReadyStaticUsage()
const morkUsage& morkUsage::kHeap = morkUsage_gHeap;

static morkUsage morkUsage_gStack; // ensure EnsureReadyStaticUsage()
const morkUsage& morkUsage::kStack = morkUsage_gStack;

static morkUsage morkUsage_gMember; // ensure EnsureReadyStaticUsage()
const morkUsage& morkUsage::kMember = morkUsage_gMember;

static morkUsage morkUsage_gGlobal; // ensure EnsureReadyStaticUsage()
const morkUsage& morkUsage::kGlobal = morkUsage_gGlobal;

static morkUsage morkUsage_gPool; // ensure EnsureReadyStaticUsage()
const morkUsage& morkUsage::kPool = morkUsage_gPool;

static morkUsage morkUsage_gNone; // ensure EnsureReadyStaticUsage()
const morkUsage& morkUsage::kNone = morkUsage_gNone;

// This must be structured to allow for non-zero values in global variables
// just before static init time.  We can only safely check for whether a
// global has the address of some other global.  Please, do not initialize
// either of the variables below to zero, because this could break when a zero
// is assigned at static init time, but after EnsureReadyStaticUsage() runs.

static mork_u4 morkUsage_g_static_init_target; // only address of this matters
static mork_u4* morkUsage_g_static_init_done; // is address of target above?

#define morkUsage_do_static_init() \
  ( morkUsage_g_static_init_done = &morkUsage_g_static_init_target )

#define morkUsage_need_static_init() \
  ( morkUsage_g_static_init_done != &morkUsage_g_static_init_target )

/*static*/
void morkUsage::EnsureReadyStaticUsage()
{
  if ( morkUsage_need_static_init() )
  {
    morkUsage_do_static_init();

    morkUsage_gHeap.InitUsage(morkUsage_kHeap);
    morkUsage_gStack.InitUsage(morkUsage_kStack);
    morkUsage_gMember.InitUsage(morkUsage_kMember);
    morkUsage_gGlobal.InitUsage(morkUsage_kGlobal);
    morkUsage_gPool.InitUsage(morkUsage_kPool);
    morkUsage_gNone.InitUsage(morkUsage_kNone);
  }
}

/*static*/
const morkUsage& morkUsage::GetHeap()   // kHeap safe at static init time
{
  EnsureReadyStaticUsage();
  return morkUsage_gHeap;
}

/*static*/
const morkUsage& morkUsage::GetStack()  // kStack safe at static init time
{
  EnsureReadyStaticUsage();
  return morkUsage_gStack;
}

/*static*/
const morkUsage& morkUsage::GetMember() // kMember safe at static init time
{
  EnsureReadyStaticUsage();
  return morkUsage_gMember;
}

/*static*/
const morkUsage& morkUsage::GetGlobal() // kGlobal safe at static init time
{
  EnsureReadyStaticUsage();
  return morkUsage_gGlobal;
}

/*static*/
const morkUsage& morkUsage::GetPool()  // kPool safe at static init time
{
  EnsureReadyStaticUsage();
  return morkUsage_gPool;
}

/*static*/
const morkUsage& morkUsage::GetNone()  // kNone safe at static init time
{
  EnsureReadyStaticUsage();
  return morkUsage_gNone;
}

morkUsage::morkUsage()
{
  if ( morkUsage_need_static_init() )
  {
    morkUsage::EnsureReadyStaticUsage();
  }
}

morkUsage::morkUsage(mork_usage code)
  : mUsage_Code(code)
{
  if ( morkUsage_need_static_init() )
  {
    morkUsage::EnsureReadyStaticUsage();
  }
}

//3456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789

/*static*/ void*
morkNode::MakeNew(size_t inSize, nsIMdbHeap& ioHeap, morkEnv* ev)
{
  void* node = 0;
  if ( &ioHeap )
  {
    ioHeap.Alloc(ev->AsMdbEnv(), inSize, (void **) &node);
    if ( !node )
      ev->OutOfMemoryError();
  }
  else
    ev->NilPointerError();
  
  return node;
}

/*static*/ void
morkNode::OnDeleteAssert(void* ioAddress) // cannot operator delete()
{
  MORK_USED_1(ioAddress);
  MORK_ASSERT(morkBool_kFalse); // tell developer: must not delete nodes
}

/*public non-poly*/ void
morkNode::ZapOld(morkEnv* ev, nsIMdbHeap* ioHeap)
  // relaces operator delete()
{
  if ( this )
  {
    if ( this->IsNode() )
    {
      this->morkNode::~morkNode(); // first call polymorphic destructor
      if ( ioHeap ) // was this node heap allocated?
        ioHeap->Free(ev->AsMdbEnv(), this);
      else if ( mNode_Usage == morkUsage_kPool )
      {
        morkHandle* h = (morkHandle*) this;
        if ( h->IsHandle() && h->GoodHandleTag() )
        {
          if ( h->mHandle_Face )
          {
            ev->mEnv_HandlePool->ZapHandle(ev, h->mHandle_Face);
          }
          else
            ev->NilPointerError();
        }
      }
    }
    else
      this->NonNodeError(ev);
  }
  else
    ev->NilPointerError();
}

/*public virtual*/ void
morkNode::CloseMorkNode(morkEnv* ev) // CloseNode() only if open
{
  if ( this->IsOpenNode() )
  {
    this->MarkClosing();
    this->CloseNode(ev);
    this->MarkShut();
  }
}

/*public virtual*/
morkNode::~morkNode() // assert that CloseNode() executed earlier
{
  MORK_ASSERT(this->IsShutNode());
  mNode_Access = morkAccess_kDead;
  mNode_Usage = morkUsage_kNone;
}

/*public virtual*/
// void CloseMorkNode(morkEnv* ev) = 0; // CloseNode() only if open
  // CloseMorkNode() is the polymorphic close method called when uses==0,
  // which must do NOTHING at all when IsOpenNode() is not true.  Otherwise,
  // CloseMorkNode() should call a static close method specific to an object.
  // Each such static close method should either call inherited static close
  // methods, or else perform the consolidated effect of calling them, where
  // subclasses should closely track any changes in base classes with care.
  

/*public non-poly*/
morkNode::morkNode(const morkUsage& inUsage, nsIMdbHeap* ioHeap)
: mNode_Heap( ioHeap )
, mNode_Base( morkBase_kNode )
, mNode_Derived ( 0 ) // until subclass sets appropriately
, mNode_Access( morkAccess_kOpen )
, mNode_Usage( inUsage.Code() )
, mNode_Mutable( morkAble_kEnabled )
, mNode_Load( morkLoad_kClean )
, mNode_Uses( 1 )
, mNode_Refs( 1 )
{
}

/*public non-poly*/
morkNode::morkNode(morkEnv* ev,
  const morkUsage& inUsage, nsIMdbHeap* ioHeap)
: mNode_Heap( ioHeap )
, mNode_Base( morkBase_kNode )
, mNode_Derived ( 0 ) // until subclass sets appropriately
, mNode_Access( morkAccess_kOpen )
, mNode_Usage( inUsage.Code() )
, mNode_Mutable( morkAble_kEnabled )
, mNode_Load( morkLoad_kClean )
, mNode_Uses( 1 )
, mNode_Refs( 1 )
{
  if ( !ioHeap && mNode_Usage == morkUsage_kHeap )
  {
    this->NilHeapError(ev);
  }
}

/*protected non-poly*/ void
morkNode::RefsUnderUsesWarning(morkEnv* ev) const 
{
  ev->NewError("mNode_Refs < mNode_Uses");
}

/*protected non-poly*/ void
morkNode::NonNodeError(morkEnv* ev) const // called when IsNode() is false
{
  ev->NewError("non-morkNode");
}

/*protected non-poly*/ void
morkNode::NonOpenNodeError(morkEnv* ev) const // when IsOpenNode() is false
{
  ev->NewError("non-open-morkNode");
}

/*protected non-poly*/ void
morkNode::NonMutableNodeError(morkEnv* ev) const // when IsMutable() is false
{
  ev->NewError("non-mutable-morkNode");
}

/*protected non-poly*/ void
morkNode::NilHeapError(morkEnv* ev) const // zero mNode_Heap w/ kHeap usage
{
  ev->NewError("nil mNode_Heap");
}

/*protected non-poly*/ void
morkNode::RefsOverflowWarning(morkEnv* ev) const // mNode_Refs overflow
{
  ev->NewWarning("mNode_Refs overflow");
}

/*protected non-poly*/ void
morkNode::UsesOverflowWarning(morkEnv* ev) const // mNode_Uses overflow
{
  ev->NewWarning("mNode_Uses overflow");
}

/*protected non-poly*/ void
morkNode::RefsUnderflowWarning(morkEnv* ev) const // mNode_Refs underflow
{
  ev->NewWarning("mNode_Refs underflow");
}

/*protected non-poly*/ void
morkNode::UsesUnderflowWarning(morkEnv* ev) const // mNode_Uses underflow
{
  ev->NewWarning("mNode_Uses underflow");
}

/*public non-poly*/ void
morkNode::CloseNode(morkEnv* ev) // called by CloseMorkNode();
{
  if ( this )
  {
    if ( this->IsNode() )
      this->MarkShut();
    else
      this->NonNodeError(ev);
  }
  else
    ev->NilPointerError();
}

  
void // utility method very similar to morkNode::SlotStrongNode():
nsIMdbHeap_SlotStrongHeap(nsIMdbHeap* self, morkEnv* ev, nsIMdbHeap** ioSlot)
  // If *ioSlot is non-nil, that heap is released by CutStrongRef() and
  // then zeroed out.  Then if this is non-nil, this is acquired by
  // calling AddStrongRef(), and if the return value shows success,
  // then this is put into slot *ioSlot.  Note self can be nil, so we
  // permit expression 'nsIMdbHeap_SlotStrongHeap(0, ev, &slot)'.
{
  nsIMdbEnv* menv = ev->AsMdbEnv();
  nsIMdbHeap* heap = *ioSlot;
  if ( heap )
  {
    *ioSlot = 0;
    heap->CutStrongRef(menv);
  }
  if ( self && ev->Good() && (self->AddStrongRef(menv)==0) && ev->Good() )
    *ioSlot = self;
}

/*public static*/ void
morkNode::SlotStrongNode(morkNode* me, morkEnv* ev, morkNode** ioSlot)
  // If *ioSlot is non-nil, that node is released by CutStrongRef() and
  // then zeroed out.  Then if me is non-nil, this is acquired by
  // calling AddStrongRef(), and if positive is returned to show success,
  // then me is put into slot *ioSlot.  Note me can be nil, so we take
  // expression 'morkNode::SlotStrongNode((morkNode*) 0, ev, &slot)'.
{
  morkNode* node = *ioSlot;
  if ( node )
  {
    *ioSlot = 0;
    node->CutStrongRef(ev);
  }
  if ( me && me->AddStrongRef(ev) )
    *ioSlot = me;
}

/*public static*/ void
morkNode::SlotWeakNode(morkNode* me, morkEnv* ev, morkNode** ioSlot)
  // If *ioSlot is non-nil, that node is released by CutWeakRef() and
  // then zeroed out.  Then if me is non-nil, this is acquired by
  // calling AddWeakRef(), and if positive is returned to show success,
  // then me is put into slot *ioSlot.  Note me can be nil, so we
  // expression 'morkNode::SlotWeakNode((morkNode*) 0, ev, &slot)'.
{
  morkNode* node = *ioSlot;
  if ( node )
  {
    *ioSlot = 0;
    node->CutWeakRef(ev);
  }
  if ( me && me->AddWeakRef(ev) )
    *ioSlot = me;
}

/*public non-poly*/ mork_uses
morkNode::AddStrongRef(morkEnv* ev)
{
  mork_uses outUses = 0;
  if ( this )
  {
    if ( this->IsNode() )
    {
      mork_uses uses = mNode_Uses;
      mork_refs refs = mNode_Refs;
      if ( refs < uses ) // need to fix broken refs/uses relation?
      { 
        this->RefsUnderUsesWarning(ev);
        mNode_Refs = mNode_Uses = refs = uses;
      }
      if ( refs < morkNode_kMaxRefCount ) // not too great?
      {
        mNode_Refs = ++refs;
        mNode_Uses = ++uses;
      }
      else
        this->RefsOverflowWarning(ev);

      outUses = uses;
    }
    else
      this->NonNodeError(ev);
  }
  else
    ev->NilPointerError();
  return outUses;
}

/*private non-poly*/ mork_bool
morkNode::cut_use_count(morkEnv* ev) // just one part of CutStrongRef()
{
  mork_bool didCut = morkBool_kFalse;
  if ( this )
  {
    if ( this->IsNode() )
    {
      mork_uses uses = mNode_Uses;
      if ( uses ) // not yet zero?
        mNode_Uses = --uses;
      else
        this->UsesUnderflowWarning(ev);

      didCut = morkBool_kTrue;
      if ( !mNode_Uses ) // last use gone? time to close node?
      {
        if ( this->IsOpenNode() )
        {
          if ( !mNode_Refs ) // no outstanding reference?
          {
            this->RefsUnderflowWarning(ev);
            ++mNode_Refs; // prevent potential crash during close
          }
          this->CloseMorkNode(ev); // polymorphic self close
          // (Note CutNode() is not polymorphic -- so don't call that.)
        }
      }
    }
    else
      this->NonNodeError(ev);
  }
  else
    ev->NilPointerError();
  return didCut;
}

/*public non-poly*/ mork_uses
morkNode::CutStrongRef(morkEnv* ev)
{
  mork_refs outRefs = 0;
  if ( this )
  {
    if ( this->IsNode() )
    {
      if ( this->cut_use_count(ev) )
        outRefs = this->CutWeakRef(ev);
    }
    else
      this->NonNodeError(ev);
  }
  else
    ev->NilPointerError();
  return outRefs;
}

/*public non-poly*/ mork_refs
morkNode::AddWeakRef(morkEnv* ev)
{
  mork_refs outRefs = 0;
  if ( this )
  {
    if ( this->IsNode() )
    {
      mork_refs refs = mNode_Refs;
      if ( refs < morkNode_kMaxRefCount ) // not too great?
        mNode_Refs = ++refs;
      else
        this->RefsOverflowWarning(ev);
        
      outRefs = refs;
    }
    else
      this->NonNodeError(ev);
  }
  else
    ev->NilPointerError();
  return outRefs;
}

/*public non-poly*/ mork_refs
morkNode::CutWeakRef(morkEnv* ev)
{
  mork_refs outRefs = 0;
  if ( this )
  {
    if ( this->IsNode() )
    {
      mork_uses uses = mNode_Uses;
      mork_refs refs = mNode_Refs;
      if ( refs ) // not yet zero?
        mNode_Refs = --refs;
      else
        this->RefsUnderflowWarning(ev);

      if ( refs < uses ) // need to fix broken refs/uses relation?
      { 
        this->RefsUnderUsesWarning(ev);
        mNode_Refs = mNode_Uses = refs = uses;
      }
        
      outRefs = refs;
      if ( !refs ) // last reference gone? time to destroy node?
        this->ZapOld(ev, mNode_Heap); // self destroy, use this no longer
    }
    else
      this->NonNodeError(ev);
  }
  else
    ev->NilPointerError();
  return outRefs;
}

static const char* morkNode_kBroken = "broken";

/*public non-poly*/ const char*
morkNode::GetNodeAccessAsString() const // e.g. "open", "shut", etc.
{
  const char* outString = morkNode_kBroken;
  switch( mNode_Access )
  {
    case morkAccess_kOpen: outString = "open"; break;
    case morkAccess_kClosing: outString = "closing"; break;
    case morkAccess_kShut: outString = "shut"; break;
    case morkAccess_kDead: outString = "dead"; break;
  }
  return outString;
}

/*public non-poly*/ const char*
morkNode::GetNodeUsageAsString() const // e.g. "heap", "stack", etc.
{
  const char* outString = morkNode_kBroken;
  switch( mNode_Usage )
  {
    case morkUsage_kHeap: outString = "heap"; break;
    case morkUsage_kStack: outString = "stack"; break;
    case morkUsage_kMember: outString = "member"; break;
    case morkUsage_kGlobal: outString = "global"; break;
    case morkUsage_kPool: outString = "pool"; break;
    case morkUsage_kNone: outString = "none"; break;
  }
  return outString;
}

//3456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789
