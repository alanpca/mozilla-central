/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
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
 * Copyright (C) 1998 Netscape Communications Corporation.  All Rights
 * Reserved.
 */
/* AUTO-GENERATED. DO NOT EDIT!!! */

#ifndef nsIDOMHTMLTextAreaElement_h__
#define nsIDOMHTMLTextAreaElement_h__

#include "nsISupports.h"
#include "nsString.h"
#include "nsIScriptContext.h"
#include "nsIDOMHTMLElement.h"

class nsIDOMHTMLFormElement;

#define NS_IDOMHTMLTEXTAREAELEMENT_IID \
{ 0x6f765324,  0xee43, 0x11d1, \
 { 0x9b, 0xc3, 0x00, 0x60, 0x08, 0x8c, 0xa6, 0xb3 } } 

class nsIDOMHTMLTextAreaElement : public nsIDOMHTMLElement {
public:

  NS_IMETHOD    GetDefaultValue(nsString& aDefaultValue)=0;
  NS_IMETHOD    SetDefaultValue(const nsString& aDefaultValue)=0;

  NS_IMETHOD    GetForm(nsIDOMHTMLFormElement** aForm)=0;
  NS_IMETHOD    SetForm(nsIDOMHTMLFormElement* aForm)=0;

  NS_IMETHOD    GetAccessKey(nsString& aAccessKey)=0;
  NS_IMETHOD    SetAccessKey(const nsString& aAccessKey)=0;

  NS_IMETHOD    GetCols(PRInt32* aCols)=0;
  NS_IMETHOD    SetCols(PRInt32 aCols)=0;

  NS_IMETHOD    GetDisabled(PRBool* aDisabled)=0;
  NS_IMETHOD    SetDisabled(PRBool aDisabled)=0;

  NS_IMETHOD    GetName(nsString& aName)=0;
  NS_IMETHOD    SetName(const nsString& aName)=0;

  NS_IMETHOD    GetReadOnly(PRBool* aReadOnly)=0;
  NS_IMETHOD    SetReadOnly(PRBool aReadOnly)=0;

  NS_IMETHOD    GetRows(PRInt32* aRows)=0;
  NS_IMETHOD    SetRows(PRInt32 aRows)=0;

  NS_IMETHOD    GetTabIndex(PRInt32* aTabIndex)=0;
  NS_IMETHOD    SetTabIndex(PRInt32 aTabIndex)=0;

  NS_IMETHOD    Blur()=0;

  NS_IMETHOD    Focus()=0;

  NS_IMETHOD    Select()=0;
};


#define NS_DECL_IDOMHTMLTEXTAREAELEMENT   \
  NS_IMETHOD    GetDefaultValue(nsString& aDefaultValue);  \
  NS_IMETHOD    SetDefaultValue(const nsString& aDefaultValue);  \
  NS_IMETHOD    GetForm(nsIDOMHTMLFormElement** aForm);  \
  NS_IMETHOD    SetForm(nsIDOMHTMLFormElement* aForm);  \
  NS_IMETHOD    GetAccessKey(nsString& aAccessKey);  \
  NS_IMETHOD    SetAccessKey(const nsString& aAccessKey);  \
  NS_IMETHOD    GetCols(PRInt32* aCols);  \
  NS_IMETHOD    SetCols(PRInt32 aCols);  \
  NS_IMETHOD    GetDisabled(PRBool* aDisabled);  \
  NS_IMETHOD    SetDisabled(PRBool aDisabled);  \
  NS_IMETHOD    GetName(nsString& aName);  \
  NS_IMETHOD    SetName(const nsString& aName);  \
  NS_IMETHOD    GetReadOnly(PRBool* aReadOnly);  \
  NS_IMETHOD    SetReadOnly(PRBool aReadOnly);  \
  NS_IMETHOD    GetRows(PRInt32* aRows);  \
  NS_IMETHOD    SetRows(PRInt32 aRows);  \
  NS_IMETHOD    GetTabIndex(PRInt32* aTabIndex);  \
  NS_IMETHOD    SetTabIndex(PRInt32 aTabIndex);  \
  NS_IMETHOD    Blur();  \
  NS_IMETHOD    Focus();  \
  NS_IMETHOD    Select();  \



#define NS_FORWARD_IDOMHTMLTEXTAREAELEMENT(superClass)  \
  NS_IMETHOD    GetDefaultValue(nsString& aDefaultValue) { return superClass::GetDefaultValue(aDefaultValue); } \
  NS_IMETHOD    SetDefaultValue(const nsString& aDefaultValue) { return superClass::SetDefaultValue(aDefaultValue); } \
  NS_IMETHOD    GetForm(nsIDOMHTMLFormElement** aForm) { return superClass::GetForm(aForm); } \
  NS_IMETHOD    SetForm(nsIDOMHTMLFormElement* aForm) { return superClass::SetForm(aForm); } \
  NS_IMETHOD    GetAccessKey(nsString& aAccessKey) { return superClass::GetAccessKey(aAccessKey); } \
  NS_IMETHOD    SetAccessKey(const nsString& aAccessKey) { return superClass::SetAccessKey(aAccessKey); } \
  NS_IMETHOD    GetCols(PRInt32* aCols) { return superClass::GetCols(aCols); } \
  NS_IMETHOD    SetCols(PRInt32 aCols) { return superClass::SetCols(aCols); } \
  NS_IMETHOD    GetDisabled(PRBool* aDisabled) { return superClass::GetDisabled(aDisabled); } \
  NS_IMETHOD    SetDisabled(PRBool aDisabled) { return superClass::SetDisabled(aDisabled); } \
  NS_IMETHOD    GetName(nsString& aName) { return superClass::GetName(aName); } \
  NS_IMETHOD    SetName(const nsString& aName) { return superClass::SetName(aName); } \
  NS_IMETHOD    GetReadOnly(PRBool* aReadOnly) { return superClass::GetReadOnly(aReadOnly); } \
  NS_IMETHOD    SetReadOnly(PRBool aReadOnly) { return superClass::SetReadOnly(aReadOnly); } \
  NS_IMETHOD    GetRows(PRInt32* aRows) { return superClass::GetRows(aRows); } \
  NS_IMETHOD    SetRows(PRInt32 aRows) { return superClass::SetRows(aRows); } \
  NS_IMETHOD    GetTabIndex(PRInt32* aTabIndex) { return superClass::GetTabIndex(aTabIndex); } \
  NS_IMETHOD    SetTabIndex(PRInt32 aTabIndex) { return superClass::SetTabIndex(aTabIndex); } \
  NS_IMETHOD    Blur() { return superClass::Blur(); }  \
  NS_IMETHOD    Focus() { return superClass::Focus(); }  \
  NS_IMETHOD    Select() { return superClass::Select(); }  \


extern nsresult NS_InitHTMLTextAreaElementClass(nsIScriptContext *aContext, void **aPrototype);

extern "C" NS_DOM nsresult NS_NewScriptHTMLTextAreaElement(nsIScriptContext *aContext, nsIDOMHTMLTextAreaElement *aSupports, nsISupports *aParent, void **aReturn);

#endif // nsIDOMHTMLTextAreaElement_h__
