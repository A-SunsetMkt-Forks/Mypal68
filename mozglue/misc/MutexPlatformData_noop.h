/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MutexPlatformData_noop_h
#define MutexPlatformData_noop_h

#if !defined(__wasi__)
#  error This code is for WASI only.
#endif

#include "mozilla/PlatformMutex.h"

struct mozilla::detail::MutexImpl::PlatformData {};

#endif  // MutexPlatformData_noop_h
