/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef vm_RegExpStaticsObject_h
#define vm_RegExpStaticsObject_h

#include "vm/JSObject.h"

namespace js {

class RegExpStatics;

class RegExpStaticsObject : public NativeObject {
  friend class js::RegExpStatics;

  enum { StaticsSlot, SlotCount };

 public:
  static const JSClass class_;

  RegExpStatics* regExpStatics() const {
    return maybePtrFromReservedSlot<RegExpStatics>(StaticsSlot);
  }

  size_t sizeOfData(mozilla::MallocSizeOf mallocSizeOf) {
    // XXX: should really call RegExpStatics::sizeOfIncludingThis() here
    // instead, but the extra memory it would measure is insignificant.
    return mallocSizeOf(regExpStatics());
  }
};

}  // namespace js

#endif /* vm_RegExpStaticsObject_h */
