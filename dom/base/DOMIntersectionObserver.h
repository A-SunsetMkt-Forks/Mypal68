/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOMIntersectionObserver_h
#define DOMIntersectionObserver_h

#include "mozilla/Attributes.h"
#include "mozilla/dom/IntersectionObserverBinding.h"
#include "mozilla/ServoStyleConsts.h"
#include "nsDOMNavigationTiming.h"
#include "nsTArray.h"

namespace mozilla {
namespace dom {

class DOMIntersectionObserver;

class DOMIntersectionObserverEntry final : public nsISupports,
                                           public nsWrapperCache {
  ~DOMIntersectionObserverEntry() = default;

 public:
  DOMIntersectionObserverEntry(nsISupports* aOwner, DOMHighResTimeStamp aTime,
                               RefPtr<DOMRect> aRootBounds,
                               RefPtr<DOMRect> aBoundingClientRect,
                               RefPtr<DOMRect> aIntersectionRect,
                               bool aIsIntersecting, Element* aTarget,
                               double aIntersectionRatio)
      : mOwner(aOwner),
        mTime(aTime),
        mRootBounds(std::move(aRootBounds)),
        mBoundingClientRect(std::move(aBoundingClientRect)),
        mIntersectionRect(std::move(aIntersectionRect)),
        mIsIntersecting(aIsIntersecting),
        mTarget(aTarget),
        mIntersectionRatio(aIntersectionRatio) {}
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(DOMIntersectionObserverEntry)

  nsISupports* GetParentObject() const { return mOwner; }

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override {
    return IntersectionObserverEntry_Binding::Wrap(aCx, this, aGivenProto);
  }

  DOMHighResTimeStamp Time() const { return mTime; }

  DOMRect* GetRootBounds() { return mRootBounds; }

  DOMRect* BoundingClientRect() { return mBoundingClientRect; }

  DOMRect* IntersectionRect() { return mIntersectionRect; }

  bool IsIntersecting() const { return mIsIntersecting; }

  double IntersectionRatio() const { return mIntersectionRatio; }

  Element* Target() { return mTarget; }

 protected:
  nsCOMPtr<nsISupports> mOwner;
  DOMHighResTimeStamp mTime;
  RefPtr<DOMRect> mRootBounds;
  RefPtr<DOMRect> mBoundingClientRect;
  RefPtr<DOMRect> mIntersectionRect;
  bool mIsIntersecting;
  RefPtr<Element> mTarget;
  double mIntersectionRatio;
};

#define NS_DOM_INTERSECTION_OBSERVER_IID             \
  {                                                  \
    0x8570a575, 0xe303, 0x4d18, {                    \
      0xb6, 0xb1, 0x4d, 0x2b, 0x49, 0xd8, 0xef, 0x94 \
    }                                                \
  }

class DOMIntersectionObserver final : public nsISupports,
                                      public nsWrapperCache {
  virtual ~DOMIntersectionObserver() { Disconnect(); }

 public:
  DOMIntersectionObserver(already_AddRefed<nsPIDOMWindowInner>&& aOwner,
                          dom::IntersectionCallback& aCb);
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(DOMIntersectionObserver)
  NS_DECLARE_STATIC_IID_ACCESSOR(NS_DOM_INTERSECTION_OBSERVER_IID)

  static already_AddRefed<DOMIntersectionObserver> Constructor(
      const GlobalObject&, dom::IntersectionCallback&, ErrorResult&);
  static already_AddRefed<DOMIntersectionObserver> Constructor(
      const GlobalObject&, dom::IntersectionCallback&,
      const IntersectionObserverInit&, ErrorResult&);

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override {
    return IntersectionObserver_Binding::Wrap(aCx, this, aGivenProto);
  }

  nsISupports* GetParentObject() const;

  nsINode* GetRoot() const { return mRoot; }

  void GetRootMargin(nsACString&);
  bool SetRootMargin(const nsACString&);

  void GetThresholds(nsTArray<double>& aRetVal);
  void Observe(Element& aTarget);
  void Unobserve(Element& aTarget);

  void UnlinkTarget(Element& aTarget);
  void Disconnect();

  void TakeRecords(nsTArray<RefPtr<DOMIntersectionObserverEntry>>& aRetVal);

  void Update(Document* aDocument, DOMHighResTimeStamp time);
  MOZ_CAN_RUN_SCRIPT void Notify();

 protected:
  void Connect();
  void QueueIntersectionObserverEntry(Element* aTarget,
                                      DOMHighResTimeStamp time,
                                      const Maybe<nsRect>& aRootRect,
                                      const nsRect& aTargetRect,
                                      const Maybe<nsRect>& aIntersectionRect,
                                      bool aIsIntersecting,
                                      double aIntersectionRatio);

  nsCOMPtr<nsPIDOMWindowInner> mOwner;
  RefPtr<Document> mDocument;
  RefPtr<dom::IntersectionCallback> mCallback;
  RefPtr<nsINode> mRoot;
  StyleRect<LengthPercentage> mRootMargin;
  nsTArray<double> mThresholds;

  // Holds raw pointers which are explicitly cleared by UnlinkTarget().
  nsTArray<Element*> mObservationTargets;

  nsTArray<RefPtr<DOMIntersectionObserverEntry>> mQueuedEntries;
  bool mConnected;
};

NS_DEFINE_STATIC_IID_ACCESSOR(DOMIntersectionObserver,
                              NS_DOM_INTERSECTION_OBSERVER_IID)

}  // namespace dom
}  // namespace mozilla

#endif
