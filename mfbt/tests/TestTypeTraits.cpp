/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Assertions.h"
#include "mozilla/TypeTraits.h"

using mozilla::IsDestructible;
using mozilla::IsSame;

class PublicDestructible {
 public:
  ~PublicDestructible();
};
class PrivateDestructible {
 private:
  ~PrivateDestructible();
};
class TrivialDestructible {};

static_assert(IsDestructible<PublicDestructible>::value,
              "public destructible class is destructible");
static_assert(!IsDestructible<PrivateDestructible>::value,
              "private destructible class is not destructible");
static_assert(IsDestructible<TrivialDestructible>::value,
              "trivial destructible class is destructible");

/*
 * Android's broken [u]intptr_t inttype macros are broken because its PRI*PTR
 * macros are defined as "ld", but sizeof(long) is 8 and sizeof(intptr_t)
 * is 4 on 32-bit Android. We redefine Android's PRI*PTR macros in
 * IntegerPrintfMacros.h and assert here that our new definitions match the
 * actual type sizes seen at compile time.
 */
#if defined(ANDROID) && !defined(__LP64__)
static_assert(mozilla::IsSame<int, intptr_t>::value,
              "emulated PRI[di]PTR definitions will be wrong");
static_assert(mozilla::IsSame<unsigned int, uintptr_t>::value,
              "emulated PRI[ouxX]PTR definitions will be wrong");
#endif

int main() { return 0; }
