/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_AutoProfilerLabel_h
#define mozilla_AutoProfilerLabel_h

#include "mozilla/Attributes.h"
#include "mozilla/GuardObjects.h"
#include "mozilla/Types.h"

// The Gecko Profiler defines AutoProfilerLabel, an RAII class for
// pushing/popping frames to/from the ProfilingStack.
//
// This file defines a class of the same name that does much the same thing,
// but which can be used in (and only in) mozglue. A different class is
// necessary because mozglue cannot directly access sProfilingStack.
//
// Note that this class is slightly slower than the other AutoProfilerLabel,
// and it lacks the macro wrappers. It also is effectively hardwired to use
// JS::ProfilingCategory::OTHER as the category pair, because that's what
// the callbacks provided by the profiler use. (Specifying the categories in
// this file would require #including ProfilingCategory.h in mozglue, which we
// don't want to do.)

namespace mozilla {

// Enter should return a pointer that will be given to Exit.
typedef void* (*ProfilerLabelEnter)(const char* aLabel,
                                    const char* aDynamicString, void* aSp);
typedef void (*ProfilerLabelExit)(void* EntryContext);

// Register callbacks that do the entry/exit work involving sProfilingStack.
MFBT_API void RegisterProfilerLabelEnterExit(ProfilerLabelEnter aEnter,
                                             ProfilerLabelExit aExit);

// This #ifdef prevents this AutoProfilerLabel from being defined in libxul,
// which would conflict with the one in the profiler.
#ifdef IMPL_MFBT

class MOZ_RAII AutoProfilerLabel {
 public:
  AutoProfilerLabel(const char* aLabel,
                    const char* aDynamicString MOZ_GUARD_OBJECT_NOTIFIER_PARAM);
  ~AutoProfilerLabel();

 private:
  MOZ_DECL_USE_GUARD_OBJECT_NOTIFIER
  void* mEntryContext;
  // Number of RegisterProfilerLabelEnterExit calls, to avoid giving an entry
  // context from one generation to the next.
  uint32_t mGeneration;
};

#endif

}  // namespace mozilla

#endif  // mozilla_AutoProfilerLabel_h
