/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_PerformanceMainThread_h
#define mozilla_dom_PerformanceMainThread_h

#include "Performance.h"
#include "PerformanceStorage.h"

namespace mozilla {
namespace dom {

class PerformanceNavigationTiming;

class PerformanceMainThread final : public Performance,
                                    public PerformanceStorage {
 public:
  PerformanceMainThread(nsPIDOMWindowInner* aWindow,
                        nsDOMNavigationTiming* aDOMTiming,
                        nsITimedChannel* aChannel, bool aPrincipal);

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS_INHERITED(PerformanceMainThread,
                                                         Performance)

  PerformanceStorage* AsPerformanceStorage() override { return this; }

  virtual PerformanceTiming* Timing() override;

  virtual PerformanceNavigation* Navigation() override;

  virtual void AddEntry(nsIHttpChannel* channel,
                        nsITimedChannel* timedChannel) override;

  virtual void SetFCPTimingEntry(PerformancePaintTiming* aEntry) override;

  TimeStamp CreationTimeStamp() const override;

  DOMHighResTimeStamp CreationTime() const override;

  virtual void GetMozMemory(JSContext* aCx,
                            JS::MutableHandle<JSObject*> aObj) override;

  virtual nsDOMNavigationTiming* GetDOMTiming() const override {
    return mDOMTiming;
  }

  virtual uint64_t GetRandomTimelineSeed() override {
    return GetDOMTiming()->GetRandomTimelineSeed();
  }

  virtual nsITimedChannel* GetChannel() const override { return mChannel; }

  // The GetEntries* methods need to be overriden in order to add the
  // the document entry of type navigation.
  virtual void GetEntries(nsTArray<RefPtr<PerformanceEntry>>& aRetval) override;
  virtual void GetEntriesByType(
      const nsAString& aEntryType,
      nsTArray<RefPtr<PerformanceEntry>>& aRetval) override;
  virtual void GetEntriesByName(
      const nsAString& aName, const Optional<nsAString>& aEntryType,
      nsTArray<RefPtr<PerformanceEntry>>& aRetval) override;

  void UpdateNavigationTimingEntry() override;
  void QueueNavigationTimingEntry() override;

 protected:
  ~PerformanceMainThread();

  void CreateNavigationTimingEntry();

  void InsertUserEntry(PerformanceEntry* aEntry) override;

  bool IsPerformanceTimingAttribute(const nsAString& aName) override;

  DOMHighResTimeStamp GetPerformanceTimingFromString(
      const nsAString& aTimingName) override;

  void DispatchBufferFullEvent() override;

  RefPtr<PerformanceNavigationTiming> mDocEntry;
  RefPtr<nsDOMNavigationTiming> mDOMTiming;
  nsCOMPtr<nsITimedChannel> mChannel;
  RefPtr<PerformanceTiming> mTiming;
  RefPtr<PerformanceNavigation> mNavigation;
  RefPtr<PerformancePaintTiming> mFCPTiming;
  JS::Heap<JSObject*> mMozMemory;
};

}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_PerformanceMainThread_h
