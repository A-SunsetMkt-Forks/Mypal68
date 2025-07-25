/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef RTCStatsReport_h_
#define RTCStatsReport_h_

#include "nsWrapperCache.h"
#include "nsCOMPtr.h"

#include "nsPIDOMWindow.h"               // nsPIDOMWindowInner
#include "mozilla/dom/ScriptSettings.h"  // AutoEntryScript
#include "nsIGlobalObject.h"
#include "js/RootingAPI.h"  // JS::Rooted
#include "js/Value.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/UniquePtr.h"
#include "prtime.h"  // PR_Now
#include "mozilla/TimeStamp.h"
#include "mozilla/dom/RTCStatsReportBinding.h"  // RTCStatsCollection

namespace mozilla {
namespace dom {

class RTCStatsTimestampMaker {
 public:
  RTCStatsTimestampMaker() = default;
  explicit RTCStatsTimestampMaker(const GlobalObject* aGlobal);
  DOMHighResTimeStamp GetNow() const;

 private:
  uint64_t mRandomTimelineSeed = 0;
  DOMHighResTimeStamp mStartWallClockRaw = (double)PR_Now() / PR_USEC_PER_MSEC;
  TimeStamp mStartMonotonic = TimeStamp::NowUnfuzzed();
};

typedef MozPromise<UniquePtr<RTCStatsCollection>, nsresult, true>
    RTCStatsPromise;

typedef MozPromise<UniquePtr<RTCStatsReportInternal>, nsresult, true>
    RTCStatsReportPromise;

class RTCStatsReport final : public nsWrapperCache {
 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(RTCStatsReport)
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_NATIVE_CLASS(RTCStatsReport)

  explicit RTCStatsReport(nsPIDOMWindowInner* aParent);

  // TODO(bug 1586109): Remove this once we no longer have to create empty
  // RTCStatsReports from JS.
  static already_AddRefed<RTCStatsReport> Constructor(
      const GlobalObject& aGlobal);

  void Incorporate(RTCStatsCollection& aStats);

  nsPIDOMWindowInner* GetParentObject() const { return mParent; }

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

 private:
  ~RTCStatsReport() = default;
  void Set(const nsAString& aKey, JS::Handle<JSObject*> aValue,
           ErrorResult& aRv);

  template <typename T>
  nsresult SetRTCStats(Sequence<T>& aValues) {
    for (T& value : aValues) {
      nsresult rv = SetRTCStats(value);
      if (NS_FAILED(rv)) {
        return rv;
      }
    }
    return NS_OK;
  }

  // We cannot just declare this as SetRTCStats(RTCStats&), because the
  // conversion function that ToJSValue uses is non-virtual.
  template <typename T>
  nsresult SetRTCStats(T& aValue) {
    static_assert(std::is_base_of<RTCStats, T>::value,
                  "SetRTCStats is for setting RTCStats only");

    if (!aValue.mId.WasPassed()) {
      return NS_OK;
    }

    const nsString key(aValue.mId.Value());

    // Cargo-culted from dom::Promise; converts aValue to a JSObject
    AutoEntryScript aes(mParent->AsGlobal()->GetGlobalJSObject(),
                        "RTCStatsReport::SetRTCStats");
    JSContext* cx = aes.cx();
    JS::Rooted<JS::Value> val(cx);
    if (!ToJSValue(cx, std::forward<T>(aValue), &val)) {
      return NS_ERROR_FAILURE;
    }
    JS::Rooted<JSObject*> jsObject(cx, &val.toObject());

    ErrorResult rv;
    Set(key, jsObject, rv);
    return rv.StealNSResult();
  }

  nsCOMPtr<nsPIDOMWindowInner> mParent;
};

}  // namespace dom
}  // namespace mozilla

#endif  // RTCStatsReport_h_
