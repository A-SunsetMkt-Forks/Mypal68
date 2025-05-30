// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


// Windows Timer Primer
//
// A good article:  http://www.ddj.com/windows/184416651
// A good mozilla bug:  http://bugzilla.mozilla.org/show_bug.cgi?id=363258
//
// The default windows timer, GetSystemTimeAsFileTime is not very precise.
// It is only good to ~15.5ms.
//
// QueryPerformanceCounter is the logical choice for a high-precision timer.
// However, it is known to be buggy on some hardware.  Specifically, it can
// sometimes "jump".  On laptops, QPC can also be very expensive to call.
// It's 3-4x slower than timeGetTime() on desktops, but can be 10x slower
// on laptops.  A unittest exists which will show the relative cost of various
// timers on any system.
//
// The next logical choice is timeGetTime().  timeGetTime has a precision of
// 1ms, but only if you call APIs (timeBeginPeriod()) which affect all other
// applications on the system.  By default, precision is only 15.5ms.
// Unfortunately, we don't want to call timeBeginPeriod because we don't
// want to affect other applications.  Further, on mobile platforms, use of
// faster multimedia timers can hurt battery life.  See the intel
// article about this here:
// http://softwarecommunity.intel.com/articles/eng/1086.htm
//
// To work around all this, we're going to generally use timeGetTime().  We
// will only increase the system-wide timer if we're not running on battery
// power.

#include "base/time/time.h"

#include <windows.h>
#include <mmsystem.h>
#include <stdint.h>

#include "base/atomicops.h"
#include "base/bit_cast.h"
#include "base/cpu.h"
#include "base/logging.h"
#include "base/synchronization/lock.h"
#include "base/threading/platform_thread.h"

using base::ThreadTicks;
using base::Time;
using base::TimeDelta;
using base::TimeTicks;

namespace {

// From MSDN, FILETIME "Contains a 64-bit value representing the number of
// 100-nanosecond intervals since January 1, 1601 (UTC)."
int64_t FileTimeToMicroseconds(const FILETIME& ft) {
  // Need to bit_cast to fix alignment, then divide by 10 to convert
  // 100-nanoseconds to microseconds. This only works on little-endian
  // machines.
  return bit_cast<int64_t, FILETIME>(ft) / 10;
}

void MicrosecondsToFileTime(int64_t us, FILETIME* ft) {
  DCHECK_GE(us, 0LL) << "Time is less than 0, negative values are not "
      "representable in FILETIME";

  // Multiply by 10 to convert microseconds to 100-nanoseconds. Bit_cast will
  // handle alignment problems. This only works on little-endian machines.
  *ft = bit_cast<FILETIME, int64_t>(us * 10);
}

int64_t CurrentWallclockMicroseconds() {
  FILETIME ft;
  ::GetSystemTimeAsFileTime(&ft);
  return FileTimeToMicroseconds(ft);
}

// Time between resampling the un-granular clock for this API.  60 seconds.
const int kMaxMillisecondsToAvoidDrift = 60 * Time::kMillisecondsPerSecond;

int64_t initial_time = 0;
TimeTicks initial_ticks;

void InitializeClock() {
  initial_ticks = TimeTicks::Now();
  initial_time = CurrentWallclockMicroseconds();
}

// The two values that ActivateHighResolutionTimer uses to set the systemwide
// timer interrupt frequency on Windows. It controls how precise timers are
// but also has a big impact on battery life.
const int kMinTimerIntervalHighResMs = 1;
const int kMinTimerIntervalLowResMs = 4;
// Track if kMinTimerIntervalHighResMs or kMinTimerIntervalLowResMs is active.
bool g_high_res_timer_enabled = false;
// How many times the high resolution timer has been called.
uint32_t g_high_res_timer_count = 0;
// Start time of the high resolution timer usage monitoring. This is needed
// to calculate the usage as percentage of the total elapsed time.
TimeTicks g_high_res_timer_usage_start;
// The cumulative time the high resolution timer has been in use since
// |g_high_res_timer_usage_start| moment.
TimeDelta g_high_res_timer_usage;
// Timestamp of the last activation change of the high resolution timer. This
// is used to calculate the cumulative usage.
TimeTicks g_high_res_timer_last_activation;
// The lock to control access to the above two variables.
base::Lock* GetHighResLock() {
  static auto* lock = new base::Lock();
  return lock;
}

// Returns a pointer to the QueryThreadCycleTime() function from Windows.
// Can't statically link to it because it is not available on XP.
using QueryThreadCycleTimePtr = decltype(::QueryThreadCycleTime)*;
QueryThreadCycleTimePtr GetQueryThreadCycleTimeFunction() {
  static const QueryThreadCycleTimePtr query_thread_cycle_time_fn =
      reinterpret_cast<QueryThreadCycleTimePtr>(::GetProcAddress(
          ::GetModuleHandle(L"kernel32.dll"), "QueryThreadCycleTime"));
  return query_thread_cycle_time_fn;
}

// Returns the current value of the performance counter.
uint64_t QPCNowRaw() {
  LARGE_INTEGER perf_counter_now = {};
  // According to the MSDN documentation for QueryPerformanceCounter(), this
  // will never fail on systems that run XP or later.
  // https://msdn.microsoft.com/library/windows/desktop/ms644904.aspx
  ::QueryPerformanceCounter(&perf_counter_now);
  return perf_counter_now.QuadPart;
}

bool SafeConvertToWord(int in, WORD* out) {
  base::CheckedNumeric<WORD> result = in;
  *out = result.ValueOrDefault(std::numeric_limits<WORD>::max());
  return result.IsValid();
}

}  // namespace

// Time -----------------------------------------------------------------------

// static
Time Time::Now() {
  if (initial_time == 0)
    InitializeClock();

  // We implement time using the high-resolution timers so that we can get
  // timeouts which are smaller than 10-15ms.  If we just used
  // CurrentWallclockMicroseconds(), we'd have the less-granular timer.
  //
  // To make this work, we initialize the clock (initial_time) and the
  // counter (initial_ctr).  To compute the initial time, we can check
  // the number of ticks that have elapsed, and compute the delta.
  //
  // To avoid any drift, we periodically resync the counters to the system
  // clock.
  while (true) {
    TimeTicks ticks = TimeTicks::Now();

    // Calculate the time elapsed since we started our timer
    TimeDelta elapsed = ticks - initial_ticks;

    // Check if enough time has elapsed that we need to resync the clock.
    if (elapsed.InMilliseconds() > kMaxMillisecondsToAvoidDrift) {
      InitializeClock();
      continue;
    }

    return Time(elapsed + Time(initial_time));
  }
}

// static
Time Time::NowFromSystemTime() {
  // Force resync.
  InitializeClock();
  return Time(initial_time);
}

// static
Time Time::FromFileTime(FILETIME ft) {
  if (bit_cast<int64_t, FILETIME>(ft) == 0)
    return Time();
  if (ft.dwHighDateTime == std::numeric_limits<DWORD>::max() &&
      ft.dwLowDateTime == std::numeric_limits<DWORD>::max())
    return Max();
  return Time(FileTimeToMicroseconds(ft));
}

FILETIME Time::ToFileTime() const {
  if (is_null())
    return bit_cast<FILETIME, int64_t>(0);
  if (is_max()) {
    FILETIME result;
    result.dwHighDateTime = std::numeric_limits<DWORD>::max();
    result.dwLowDateTime = std::numeric_limits<DWORD>::max();
    return result;
  }
  FILETIME utc_ft;
  MicrosecondsToFileTime(us_, &utc_ft);
  return utc_ft;
}

// static
void Time::EnableHighResolutionTimer(bool enable) {
  base::AutoLock lock(*GetHighResLock());
  if (g_high_res_timer_enabled == enable)
    return;
  g_high_res_timer_enabled = enable;
  if (!g_high_res_timer_count)
    return;
  // Since g_high_res_timer_count != 0, an ActivateHighResolutionTimer(true)
  // was called which called timeBeginPeriod with g_high_res_timer_enabled
  // with a value which is the opposite of |enable|. With that information we
  // call timeEndPeriod with the same value used in timeBeginPeriod and
  // therefore undo the period effect.
  if (enable) {
    timeEndPeriod(kMinTimerIntervalLowResMs);
    timeBeginPeriod(kMinTimerIntervalHighResMs);
  } else {
    timeEndPeriod(kMinTimerIntervalHighResMs);
    timeBeginPeriod(kMinTimerIntervalLowResMs);
  }
}

// static
bool Time::ActivateHighResolutionTimer(bool activating) {
  // We only do work on the transition from zero to one or one to zero so we
  // can easily undo the effect (if necessary) when EnableHighResolutionTimer is
  // called.
  const uint32_t max = std::numeric_limits<uint32_t>::max();

  base::AutoLock lock(*GetHighResLock());
  UINT period = g_high_res_timer_enabled ? kMinTimerIntervalHighResMs
                                         : kMinTimerIntervalLowResMs;
  if (activating) {
    DCHECK_NE(g_high_res_timer_count, max);
    ++g_high_res_timer_count;
    if (g_high_res_timer_count == 1) {
      g_high_res_timer_last_activation = TimeTicks::Now();
      timeBeginPeriod(period);
    }
  } else {
    DCHECK_NE(g_high_res_timer_count, 0u);
    --g_high_res_timer_count;
    if (g_high_res_timer_count == 0) {
      g_high_res_timer_usage +=
          TimeTicks::Now() - g_high_res_timer_last_activation;
      timeEndPeriod(period);
    }
  }
  return (period == kMinTimerIntervalHighResMs);
}

// static
bool Time::IsHighResolutionTimerInUse() {
  base::AutoLock lock(*GetHighResLock());
  return g_high_res_timer_enabled && g_high_res_timer_count > 0;
}

// static
void Time::ResetHighResolutionTimerUsage() {
  base::AutoLock lock(*GetHighResLock());
  g_high_res_timer_usage = TimeDelta();
  g_high_res_timer_usage_start = TimeTicks::Now();
  if (g_high_res_timer_count > 0)
    g_high_res_timer_last_activation = g_high_res_timer_usage_start;
}

// static
double Time::GetHighResolutionTimerUsage() {
  base::AutoLock lock(*GetHighResLock());
  TimeTicks now = TimeTicks::Now();
  TimeDelta elapsed_time = now - g_high_res_timer_usage_start;
  if (elapsed_time.is_zero()) {
    // This is unexpected but possible if TimeTicks resolution is low and
    // GetHighResolutionTimerUsage() is called promptly after
    // ResetHighResolutionTimerUsage().
    return 0.0;
  }
  TimeDelta used_time = g_high_res_timer_usage;
  if (g_high_res_timer_count > 0) {
    // If currently activated add the remainder of time since the last
    // activation.
    used_time += now - g_high_res_timer_last_activation;
  }
  return used_time.InMillisecondsF() / elapsed_time.InMillisecondsF() * 100;
}

// static
bool Time::FromExploded(bool is_local, const Exploded& exploded, Time* time) {
  // Create the system struct representing our exploded time. It will either be
  // in local time or UTC.If casting from int to WORD results in overflow,
  // fail and return Time(0).
  SYSTEMTIME st;
  if (!SafeConvertToWord(exploded.year, &st.wYear) ||
      !SafeConvertToWord(exploded.month, &st.wMonth) ||
      !SafeConvertToWord(exploded.day_of_week, &st.wDayOfWeek) ||
      !SafeConvertToWord(exploded.day_of_month, &st.wDay) ||
      !SafeConvertToWord(exploded.hour, &st.wHour) ||
      !SafeConvertToWord(exploded.minute, &st.wMinute) ||
      !SafeConvertToWord(exploded.second, &st.wSecond) ||
      !SafeConvertToWord(exploded.millisecond, &st.wMilliseconds)) {
    *time = base::Time(0);
    return false;
  }

  FILETIME ft;
  bool success = true;
  // Ensure that it's in UTC.
  if (is_local) {
    SYSTEMTIME utc_st;
    success = TzSpecificLocalTimeToSystemTime(nullptr, &st, &utc_st) &&
              SystemTimeToFileTime(&utc_st, &ft);
  } else {
    success = !!SystemTimeToFileTime(&st, &ft);
  }

  if (!success) {
    *time = Time(0);
    return false;
  }

  *time = Time(FileTimeToMicroseconds(ft));
  return true;
}

void Time::Explode(bool is_local, Exploded* exploded) const {
  if (us_ < 0LL) {
    // We are not able to convert it to FILETIME.
    ZeroMemory(exploded, sizeof(*exploded));
    return;
  }

  // FILETIME in UTC.
  FILETIME utc_ft;
  MicrosecondsToFileTime(us_, &utc_ft);

  // FILETIME in local time if necessary.
  bool success = true;
  // FILETIME in SYSTEMTIME (exploded).
  SYSTEMTIME st = {0};
  if (is_local) {
    SYSTEMTIME utc_st;
    // We don't use FileTimeToLocalFileTime here, since it uses the current
    // settings for the time zone and daylight saving time. Therefore, if it is
    // daylight saving time, it will take daylight saving time into account,
    // even if the time you are converting is in standard time.
    success = FileTimeToSystemTime(&utc_ft, &utc_st) &&
              SystemTimeToTzSpecificLocalTime(nullptr, &utc_st, &st);
  } else {
    success = !!FileTimeToSystemTime(&utc_ft, &st);
  }

  if (!success) {
    NOTREACHED() << "Unable to convert time, don't know why";
    ZeroMemory(exploded, sizeof(*exploded));
    return;
  }

  exploded->year = st.wYear;
  exploded->month = st.wMonth;
  exploded->day_of_week = st.wDayOfWeek;
  exploded->day_of_month = st.wDay;
  exploded->hour = st.wHour;
  exploded->minute = st.wMinute;
  exploded->second = st.wSecond;
  exploded->millisecond = st.wMilliseconds;
}

// TimeTicks ------------------------------------------------------------------
namespace {

// We define a wrapper to adapt between the __stdcall and __cdecl call of the
// mock function, and to avoid a static constructor.  Assigning an import to a
// function pointer directly would require setup code to fetch from the IAT.
DWORD timeGetTimeWrapper() {
  return timeGetTime();
}

DWORD (*g_tick_function)(void) = &timeGetTimeWrapper;

// A structure holding the most significant bits of "last seen" and a
// "rollover" counter.
union LastTimeAndRolloversState {
  // The state as a single 32-bit opaque value.
  base::subtle::Atomic32 as_opaque_32;

  // The state as usable values.
  struct {
    // The top 8-bits of the "last" time. This is enough to check for rollovers
    // and the small bit-size means fewer CompareAndSwap operations to store
    // changes in state, which in turn makes for fewer retries.
    uint8_t last_8;
    // A count of the number of detected rollovers. Using this as bits 47-32
    // of the upper half of a 64-bit value results in a 48-bit tick counter.
    // This extends the total rollover period from about 49 days to about 8800
    // years while still allowing it to be stored with last_8 in a single
    // 32-bit value.
    uint16_t rollovers;
  } as_values;
};
base::subtle::Atomic32 g_last_time_and_rollovers = 0;
static_assert(
    sizeof(LastTimeAndRolloversState) <= sizeof(g_last_time_and_rollovers),
    "LastTimeAndRolloversState does not fit in a single atomic word");

// We use timeGetTime() to implement TimeTicks::Now().  This can be problematic
// because it returns the number of milliseconds since Windows has started,
// which will roll over the 32-bit value every ~49 days.  We try to track
// rollover ourselves, which works if TimeTicks::Now() is called at least every
// 48.8 days (not 49 days because only changes in the top 8 bits get noticed).
TimeDelta RolloverProtectedNow() {
  LastTimeAndRolloversState state;
  DWORD now;  // DWORD is always unsigned 32 bits.

  while (true) {
    // Fetch the "now" and "last" tick values, updating "last" with "now" and
    // incrementing the "rollovers" counter if the tick-value has wrapped back
    // around. Atomic operations ensure that both "last" and "rollovers" are
    // always updated together.
    int32_t original = base::subtle::Acquire_Load(&g_last_time_and_rollovers);
    state.as_opaque_32 = original;
    now = g_tick_function();
    uint8_t now_8 = static_cast<uint8_t>(now >> 24);
    if (now_8 < state.as_values.last_8)
      ++state.as_values.rollovers;
    state.as_values.last_8 = now_8;

    // If the state hasn't changed, exit the loop.
    if (state.as_opaque_32 == original)
      break;

    // Save the changed state. If the existing value is unchanged from the
    // original, exit the loop.
    int32_t check = base::subtle::Release_CompareAndSwap(
        &g_last_time_and_rollovers, original, state.as_opaque_32);
    if (check == original)
      break;

    // Another thread has done something in between so retry from the top.
  }

  return TimeDelta::FromMilliseconds(
      now + (static_cast<uint64_t>(state.as_values.rollovers) << 32));
}

// Discussion of tick counter options on Windows:
//
// (1) CPU cycle counter. (Retrieved via RDTSC)
// The CPU counter provides the highest resolution time stamp and is the least
// expensive to retrieve. However, on older CPUs, two issues can affect its
// reliability: First it is maintained per processor and not synchronized
// between processors. Also, the counters will change frequency due to thermal
// and power changes, and stop in some states.
//
// (2) QueryPerformanceCounter (QPC). The QPC counter provides a high-
// resolution (<1 microsecond) time stamp. On most hardware running today, it
// auto-detects and uses the constant-rate RDTSC counter to provide extremely
// efficient and reliable time stamps.
//
// On older CPUs where RDTSC is unreliable, it falls back to using more
// expensive (20X to 40X more costly) alternate clocks, such as HPET or the ACPI
// PM timer, and can involve system calls; and all this is up to the HAL (with
// some help from ACPI). According to
// http://blogs.msdn.com/oldnewthing/archive/2005/09/02/459952.aspx, in the
// worst case, it gets the counter from the rollover interrupt on the
// programmable interrupt timer. In best cases, the HAL may conclude that the
// RDTSC counter runs at a constant frequency, then it uses that instead. On
// multiprocessor machines, it will try to verify the values returned from
// RDTSC on each processor are consistent with each other, and apply a handful
// of workarounds for known buggy hardware. In other words, QPC is supposed to
// give consistent results on a multiprocessor computer, but for older CPUs it
// can be unreliable due bugs in BIOS or HAL.
//
// (3) System time. The system time provides a low-resolution (from ~1 to ~15.6
// milliseconds) time stamp but is comparatively less expensive to retrieve and
// more reliable. Time::EnableHighResolutionTimer() and
// Time::ActivateHighResolutionTimer() can be called to alter the resolution of
// this timer; and also other Windows applications can alter it, affecting this
// one.

using NowFunction = TimeDelta (*)(void);

TimeDelta InitialNowFunction();

// See "threading notes" in InitializeNowFunctionPointer() for details on how
// concurrent reads/writes to these globals has been made safe.
NowFunction g_now_function = &InitialNowFunction;
int64_t g_qpc_ticks_per_second = 0;

// As of January 2015, use of <atomic> is forbidden in Chromium code. This is
// what std::atomic_thread_fence does on Windows on all Intel architectures when
// the memory_order argument is anything but std::memory_order_seq_cst:
#define ATOMIC_THREAD_FENCE(memory_order) _ReadWriteBarrier();

TimeDelta QPCValueToTimeDelta(LONGLONG qpc_value) {
  // Ensure that the assignment to |g_qpc_ticks_per_second|, made in
  // InitializeNowFunctionPointer(), has happened by this point.
  ATOMIC_THREAD_FENCE(memory_order_acquire);

  DCHECK_GT(g_qpc_ticks_per_second, 0);

  // If the QPC Value is below the overflow threshold, we proceed with
  // simple multiply and divide.
  if (qpc_value < Time::kQPCOverflowThreshold) {
    return TimeDelta::FromMicroseconds(
        qpc_value * Time::kMicrosecondsPerSecond / g_qpc_ticks_per_second);
  }
  // Otherwise, calculate microseconds in a round about manner to avoid
  // overflow and precision issues.
  int64_t whole_seconds = qpc_value / g_qpc_ticks_per_second;
  int64_t leftover_ticks = qpc_value - (whole_seconds * g_qpc_ticks_per_second);
  return TimeDelta::FromMicroseconds(
      (whole_seconds * Time::kMicrosecondsPerSecond) +
      ((leftover_ticks * Time::kMicrosecondsPerSecond) /
       g_qpc_ticks_per_second));
}

TimeDelta QPCNow() {
  return QPCValueToTimeDelta(QPCNowRaw());
}

bool IsBuggyAthlon(const base::CPU& cpu) {
  // On Athlon X2 CPUs (e.g. model 15) QueryPerformanceCounter is unreliable.
  return cpu.vendor_name() == "AuthenticAMD" && cpu.family() == 15;
}

void InitializeNowFunctionPointer() {
  LARGE_INTEGER ticks_per_sec = {};
  if (!QueryPerformanceFrequency(&ticks_per_sec))
    ticks_per_sec.QuadPart = 0;

  // If Windows cannot provide a QPC implementation, TimeTicks::Now() must use
  // the low-resolution clock.
  //
  // If the QPC implementation is expensive and/or unreliable, TimeTicks::Now()
  // will still use the low-resolution clock. A CPU lacking a non-stop time
  // counter will cause Windows to provide an alternate QPC implementation that
  // works, but is expensive to use. Certain Athlon CPUs are known to make the
  // QPC implementation unreliable.
  //
  // Otherwise, Now uses the high-resolution QPC clock. As of 21 August 2015,
  // ~72% of users fall within this category.
  NowFunction now_function;
  base::CPU cpu;
  if (ticks_per_sec.QuadPart <= 0 ||
      !cpu.has_non_stop_time_stamp_counter() || IsBuggyAthlon(cpu)) {
    now_function = &RolloverProtectedNow;
  } else {
    now_function = &QPCNow;
  }

  // Threading note 1: In an unlikely race condition, it's possible for two or
  // more threads to enter InitializeNowFunctionPointer() in parallel. This is
  // not a problem since all threads should end up writing out the same values
  // to the global variables.
  //
  // Threading note 2: A release fence is placed here to ensure, from the
  // perspective of other threads using the function pointers, that the
  // assignment to |g_qpc_ticks_per_second| happens before the function pointers
  // are changed.
  g_qpc_ticks_per_second = ticks_per_sec.QuadPart;
  ATOMIC_THREAD_FENCE(memory_order_release);
  g_now_function = now_function;
}

TimeDelta InitialNowFunction() {
  InitializeNowFunctionPointer();
  return g_now_function();
}

}  // namespace

// static
TimeTicks::TickFunctionType TimeTicks::SetMockTickFunction(
    TickFunctionType ticker) {
  TickFunctionType old = g_tick_function;
  g_tick_function = ticker;
  base::subtle::NoBarrier_Store(&g_last_time_and_rollovers, 0);
  return old;
}

// static
TimeTicks TimeTicks::Now() {
  return TimeTicks() + g_now_function();
}

// static
bool TimeTicks::IsHighResolution() {
  if (g_now_function == &InitialNowFunction)
    InitializeNowFunctionPointer();
  return g_now_function == &QPCNow;
}

// static
bool TimeTicks::IsConsistentAcrossProcesses() {
  // According to Windows documentation [1] QPC is consistent post-Windows
  // Vista. So if we are using QPC then we are consistent which is the same as
  // being high resolution.
  //
  // [1] https://msdn.microsoft.com/en-us/library/windows/desktop/dn553408(v=vs.85).aspx
  //
  // "In general, the performance counter results are consistent across all
  // processors in multi-core and multi-processor systems, even when measured on
  // different threads or processes. Here are some exceptions to this rule:
  // - Pre-Windows Vista operating systems that run on certain processors might
  // violate this consistency because of one of these reasons:
  //     1. The hardware processors have a non-invariant TSC and the BIOS
  //     doesn't indicate this condition correctly.
  //     2. The TSC synchronization algorithm that was used wasn't suitable for
  //     systems with large numbers of processors."
  return IsHighResolution();
}

// static
TimeTicks::Clock TimeTicks::GetClock() {
  return IsHighResolution() ?
      Clock::WIN_QPC : Clock::WIN_ROLLOVER_PROTECTED_TIME_GET_TIME;
}

// static
ThreadTicks ThreadTicks::Now() {
  DCHECK(IsSupported());

  // Get the number of TSC ticks used by the current thread.
  ULONG64 thread_cycle_time = 0;
  GetQueryThreadCycleTimeFunction()(::GetCurrentThread(), &thread_cycle_time);

  // Get the frequency of the TSC.
  double tsc_ticks_per_second = TSCTicksPerSecond();
  if (tsc_ticks_per_second == 0)
    return ThreadTicks();

  // Return the CPU time of the current thread.
  double thread_time_seconds = thread_cycle_time / tsc_ticks_per_second;
  return ThreadTicks(
      static_cast<int64_t>(thread_time_seconds * Time::kMicrosecondsPerSecond));
}

// static
bool ThreadTicks::IsSupportedWin() {
  static bool is_supported = base::CPU().has_non_stop_time_stamp_counter() &&
                             !IsBuggyAthlon(base::CPU());
  return is_supported;
}

// static
void ThreadTicks::WaitUntilInitializedWin() {
  while (TSCTicksPerSecond() == 0)
    ::Sleep(10);
}

#if defined(_M_ARM64)
#define ReadCycleCounter() _ReadStatusReg(ARM64_PMCCNTR_EL0)
#else
#define ReadCycleCounter() __rdtsc()
#endif

double ThreadTicks::TSCTicksPerSecond() {
  DCHECK(IsSupported());

  // The value returned by QueryPerformanceFrequency() cannot be used as the TSC
  // frequency, because there is no guarantee that the TSC frequency is equal to
  // the performance counter frequency.

  // The TSC frequency is cached in a static variable because it takes some time
  // to compute it.
  static double tsc_ticks_per_second = 0;
  if (tsc_ticks_per_second != 0)
    return tsc_ticks_per_second;

  // Increase the thread priority to reduces the chances of having a context
  // switch during a reading of the TSC and the performance counter.
  int previous_priority = ::GetThreadPriority(::GetCurrentThread());
  ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

  // The first time that this function is called, make an initial reading of the
  // TSC and the performance counter.
  static const uint64_t tsc_initial = ReadCycleCounter();
  static const uint64_t perf_counter_initial = QPCNowRaw();

  // Make a another reading of the TSC and the performance counter every time
  // that this function is called.
  uint64_t tsc_now = ReadCycleCounter();
  uint64_t perf_counter_now = QPCNowRaw();

  // Reset the thread priority.
  ::SetThreadPriority(::GetCurrentThread(), previous_priority);

  // Make sure that at least 50 ms elapsed between the 2 readings. The first
  // time that this function is called, we don't expect this to be the case.
  // Note: The longer the elapsed time between the 2 readings is, the more
  //   accurate the computed TSC frequency will be. The 50 ms value was
  //   chosen because local benchmarks show that it allows us to get a
  //   stddev of less than 1 tick/us between multiple runs.
  // Note: According to the MSDN documentation for QueryPerformanceFrequency(),
  //   this will never fail on systems that run XP or later.
  //   https://msdn.microsoft.com/library/windows/desktop/ms644905.aspx
  LARGE_INTEGER perf_counter_frequency = {};
  ::QueryPerformanceFrequency(&perf_counter_frequency);
  DCHECK_GE(perf_counter_now, perf_counter_initial);
  uint64_t perf_counter_ticks = perf_counter_now - perf_counter_initial;
  double elapsed_time_seconds =
      perf_counter_ticks / static_cast<double>(perf_counter_frequency.QuadPart);

  const double kMinimumEvaluationPeriodSeconds = 0.05;
  if (elapsed_time_seconds < kMinimumEvaluationPeriodSeconds)
    return 0;

  // Compute the frequency of the TSC.
  DCHECK_GE(tsc_now, tsc_initial);
  uint64_t tsc_ticks = tsc_now - tsc_initial;
  tsc_ticks_per_second = tsc_ticks / elapsed_time_seconds;

  return tsc_ticks_per_second;
}

#undef ReadCycleCounter

// static
TimeTicks TimeTicks::FromQPCValue(LONGLONG qpc_value) {
  return TimeTicks() + QPCValueToTimeDelta(qpc_value);
}

// TimeDelta ------------------------------------------------------------------

// static
TimeDelta TimeDelta::FromQPCValue(LONGLONG qpc_value) {
  return QPCValueToTimeDelta(qpc_value);
}
