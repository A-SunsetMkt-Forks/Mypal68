/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef vm_BuiltinObjectKind_h
#define vm_BuiltinObjectKind_h

#include <stdint.h>

#include "jstypes.h"

#include "frontend/ParserAtom.h"  // TaggedParserAtomIndex

class JS_PUBLIC_API JSAtom;
struct JS_PUBLIC_API JSContext;
class JS_PUBLIC_API JSObject;

namespace js {

class GlobalObject;

/**
 * Built-in objects used by the GetBuiltinConstructor and GetBuiltinPrototype
 * self-hosted intrinsics.
 */
enum class BuiltinObjectKind : uint8_t {
  // Built-in constructors.
  Array,
  ArrayBuffer,
  Int32Array,
  Iterator,
  Promise,
  RegExp,
  SharedArrayBuffer,
  Symbol,

  // Built-in prototypes.
  FunctionPrototype,
  ObjectPrototype,
  RegExpPrototype,
  StringPrototype,

  // Built-in Intl prototypes.
  DateTimeFormatPrototype,
  NumberFormatPrototype,

  // Invalid placeholder.
  None,
};

/**
 * Return the BuiltinObjectKind for the given constructor name. Return
 * BuiltinObjectKind::None if no matching constructor was found.
 */
BuiltinObjectKind BuiltinConstructorForName(
    frontend::TaggedParserAtomIndex name);

/**
 * Return the BuiltinObjectKind for the given prototype name. Return
 * BuiltinObjectKind::None if no matching prototype was found.
 */
BuiltinObjectKind BuiltinPrototypeForName(frontend::TaggedParserAtomIndex name);

/**
 * Return the built-in object if already created for the given global. Otherwise
 * return nullptr.
 */
JSObject* MaybeGetBuiltinObject(GlobalObject* global, BuiltinObjectKind kind);

/**
 * Return the built-in object for the given global.
 */
JSObject* GetOrCreateBuiltinObject(JSContext* cx, BuiltinObjectKind kind);

/**
 * Return the display name for a built-in object.
 */
const char* BuiltinObjectName(BuiltinObjectKind kind);

}  // namespace js

#endif /* vm_BuiltinObjectKind_h */
