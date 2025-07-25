/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef vm_ObjectFlags_h
#define vm_ObjectFlags_h

#include "util/EnumFlags.h"  // js::EnumFlags

namespace js {

// Flags set on the Shape which describe the referring object. Once set these
// cannot be unset (except during object densification of sparse indexes), and
// are transferred from shape to shape as the object's last property changes.
//
// If you add a new flag here, please add appropriate code to JSObject::dump to
// dump it as part of the object representation.
enum class ObjectFlag : uint16_t {
  IsUsedAsPrototype = 1 << 0,
  NotExtensible = 1 << 1,
  Indexed = 1 << 2,
  HasInterestingSymbol = 1 << 3,
  // (1 << 4) is unused.
  FrozenElements = 1 << 5,  // See ObjectElements::FROZEN comment.
  UncacheableProto = 1 << 6,
  ImmutablePrototype = 1 << 7,

  // See JSObject::isQualifiedVarObj().
  QualifiedVarObj = 1 << 8,

  // If set, the object may have a non-writable property or an accessor
  // property.
  //
  // * This is only set for PlainObjects because we only need it for these
  //   objects and setting it for other objects confuses insertInitialShape.
  //
  // * This flag does not account for properties named "__proto__". This is
  //   because |Object.prototype| has a "__proto__" accessor property and we
  //   don't want to include it because it would result in the flag being set on
  //   most proto chains. Code using this flag must check for "__proto__"
  //   property names separately.
  HasNonWritableOrAccessorPropExclProto = 1 << 9,

  // If set, the object either mutated or deleted an accessor property. This is
  // used to invalidate IC/Warp code specializing on specific getter/setter
  // objects. See also the SMDOC comment in vm/GetterSetter.h.
  HadGetterSetterChange = 1 << 10,
};

using ObjectFlags = EnumFlags<ObjectFlag>;

}  // namespace js

#endif /* vm_ObjectFlags_h */
