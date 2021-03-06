/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sw=4 et tw=78:
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef StringObject_h___
#define StringObject_h___

#include "jsobj.h"
#include "jsstr.h"

namespace js {

class StringObject : public JSObject
{
    static const unsigned PRIMITIVE_VALUE_SLOT = 0;
    static const unsigned LENGTH_SLOT = 1;

  public:
    static const unsigned RESERVED_SLOTS = 2;

    /*
     * Creates a new String object boxing the given string.  The object's
     * [[Prototype]] is determined from context.
     */
    static inline StringObject *create(JSContext *cx, HandleString str);

    /*
     * Identical to create(), but uses |proto| as [[Prototype]].  This method
     * must not be used to create |String.prototype|.
     */
    static inline StringObject *createWithProto(JSContext *cx, HandleString str, JSObject &proto);

    JSString *unbox() const {
        return getFixedSlot(PRIMITIVE_VALUE_SLOT).toString();
    }

    inline size_t length() const {
        return size_t(getFixedSlot(LENGTH_SLOT).toInt32());
    }

    static size_t offsetOfPrimitiveValue() {
        return getFixedSlotOffset(PRIMITIVE_VALUE_SLOT);
    }
    static size_t offsetOfLength() {
        return getFixedSlotOffset(LENGTH_SLOT);
    }

  private:
    inline bool init(JSContext *cx, HandleString str);

    void setStringThis(JSString *str) {
        JS_ASSERT(getReservedSlot(PRIMITIVE_VALUE_SLOT).isUndefined());
        setFixedSlot(PRIMITIVE_VALUE_SLOT, StringValue(str));
        setFixedSlot(LENGTH_SLOT, Int32Value(int32_t(str->length())));
    }

    /* For access to init, as String.prototype is special. */
    friend JSObject *
    ::js_InitStringClass(JSContext *cx, js::HandleObject global);

    /*
     * Compute the initial shape to associate with fresh String objects, which
     * encodes the initial length property. Return the shape after changing
     * this String object's last property to it.
     */
    Shape *assignInitialShape(JSContext *cx);
};

} // namespace js

#endif /* StringObject_h__ */
