/* 
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.0 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and limitations
 * under the License.
 *
 * The Initial Developer of the Original Code is Sun Microsystems,
 * Inc. Portions created by Sun are Copyright (C) 1999 Sun Microsystems,
 * Inc. All Rights Reserved. 
 */
#ifndef __PlugletInputStream_h__
#define __PlugletInputStream_h__
#include "jni.h"
#include "nsIInputStream.h"

class PlugletInputStream {
 public:
    static jobject GetJObject(const nsIInputStream *stream);
 private:
    static void Initialize(void);
    static void Destroy(void);
    static jclass    clazz;
    static jmethodID initMID;
};
#endif /*  __PlugletInputStream_h__ */
