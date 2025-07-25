/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_serviceworkerregistrationimpl_h
#define mozilla_dom_serviceworkerregistrationimpl_h

#include "ServiceWorkerRegistration.h"
#include "ServiceWorkerRegistrationListener.h"
#include "mozilla/Assertions.h"
#include "mozilla/Mutex.h"
#include "mozilla/RefPtr.h"
#include "mozilla/dom/ServiceWorkerRegistrationDescriptor.h"
#include "mozilla/dom/ServiceWorkerUtils.h"
#include "nsISupports.h"
#include "nsStringFwd.h"

namespace mozilla {
namespace dom {

class ServiceWorkerRegistrationInfo;
class WeakWorkerRef;
class WorkerPrivate;

////////////////////////////////////////////////////
// Main Thread implementation

class ServiceWorkerRegistrationMainThread final
    : public ServiceWorkerRegistration::Inner,
      public ServiceWorkerRegistrationListener {
 public:
  NS_INLINE_DECL_REFCOUNTING(ServiceWorkerRegistrationMainThread, override)

  explicit ServiceWorkerRegistrationMainThread(
      const ServiceWorkerRegistrationDescriptor& aDescriptor);

  // ServiceWorkerRegistration::Inner
  void SetServiceWorkerRegistration(ServiceWorkerRegistration* aReg) override;

  void ClearServiceWorkerRegistration(ServiceWorkerRegistration* aReg) override;

  void Update(const nsCString& aNewestWorkerScriptUrl,
              ServiceWorkerRegistrationCallback&& aSuccessCB,
              ServiceWorkerFailureCallback&& aFailureCB) override;

  void Unregister(ServiceWorkerBoolCallback&& aSuccessCB,
                  ServiceWorkerFailureCallback&& aFailureCB) override;

  // ServiceWorkerRegistrationListener
  void UpdateState(
      const ServiceWorkerRegistrationDescriptor& aDescriptor) override;

  void FireUpdateFound() override;

  void RegistrationCleared() override;

  void GetScope(nsAString& aScope) const override { aScope = mScope; }

  bool MatchesDescriptor(
      const ServiceWorkerRegistrationDescriptor& aDescriptor) override;

 private:
  ~ServiceWorkerRegistrationMainThread();

  void StartListeningForEvents();

  void StopListeningForEvents();

  void RegistrationClearedInternal();

  ServiceWorkerRegistration* mOuter;
  ServiceWorkerRegistrationDescriptor mDescriptor;
  RefPtr<ServiceWorkerRegistrationInfo> mInfo;
  const nsString mScope;
  bool mListeningForEvents;
};

////////////////////////////////////////////////////
// Worker Thread implementation

class WorkerListener;

class ServiceWorkerRegistrationWorkerThread final
    : public ServiceWorkerRegistration::Inner {
  friend class WorkerListener;

 public:
  NS_INLINE_DECL_REFCOUNTING(ServiceWorkerRegistrationWorkerThread, override)

  explicit ServiceWorkerRegistrationWorkerThread(
      const ServiceWorkerRegistrationDescriptor& aDescriptor);

  void RegistrationCleared();

  // ServiceWorkerRegistration::Inner
  void SetServiceWorkerRegistration(ServiceWorkerRegistration* aReg) override;

  void ClearServiceWorkerRegistration(ServiceWorkerRegistration* aReg) override;

  void Update(const nsCString& aNewestWorkerScriptUrl,
              ServiceWorkerRegistrationCallback&& aSuccessCB,
              ServiceWorkerFailureCallback&& aFailureCB) override;

  void Unregister(ServiceWorkerBoolCallback&& aSuccessCB,
                  ServiceWorkerFailureCallback&& aFailureCB) override;

 private:
  ~ServiceWorkerRegistrationWorkerThread();

  void InitListener();

  void ReleaseListener();

  void UpdateState(const ServiceWorkerRegistrationDescriptor& aDescriptor);

  void FireUpdateFound();

  // This can be called only by WorkerListener.
  WorkerPrivate* GetWorkerPrivate(const MutexAutoLock& aProofOfLock);

  ServiceWorkerRegistration* mOuter;
  const ServiceWorkerRegistrationDescriptor mDescriptor;
  const nsString mScope;
  RefPtr<WorkerListener> mListener;
  RefPtr<WeakWorkerRef> mWorkerRef;
};

}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_serviceworkerregistrationimpl_h
