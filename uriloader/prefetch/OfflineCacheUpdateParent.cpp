/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "OfflineCacheUpdateParent.h"

#include "BackgroundUtils.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/ipc/URIUtils.h"
#include "mozilla/Unused.h"
#include "nsContentUtils.h"
#include "nsOfflineCacheUpdate.h"
#include "nsIApplicationCache.h"
#include "nsNetUtil.h"

using namespace mozilla::ipc;
using mozilla::BasePrincipal;
using mozilla::OriginAttributes;
using mozilla::dom::BrowserParent;

//
// To enable logging (see mozilla/Logging.h for full details):
//
//    set MOZ_LOG=nsOfflineCacheUpdate:5
//    set MOZ_LOG_FILE=offlineupdate.log
//
// this enables LogLevel::Debug level information and places all output in
// the file offlineupdate.log
//
extern mozilla::LazyLogModule gOfflineCacheUpdateLog;

#undef LOG
#define LOG(args) \
  MOZ_LOG(gOfflineCacheUpdateLog, mozilla::LogLevel::Debug, args)

#undef LOG_ENABLED
#define LOG_ENABLED() \
  MOZ_LOG_TEST(gOfflineCacheUpdateLog, mozilla::LogLevel::Debug)

namespace mozilla {
namespace docshell {

//-----------------------------------------------------------------------------
// OfflineCacheUpdateParent::nsISupports
//-----------------------------------------------------------------------------

NS_IMPL_ISUPPORTS(OfflineCacheUpdateParent, nsIOfflineCacheUpdateObserver,
                  nsILoadContext)

//-----------------------------------------------------------------------------
// OfflineCacheUpdateParent <public>
//-----------------------------------------------------------------------------

OfflineCacheUpdateParent::OfflineCacheUpdateParent() : mIPCClosed(false) {
  // Make sure the service has been initialized
  nsOfflineCacheUpdateService::EnsureService();

  LOG(("OfflineCacheUpdateParent::OfflineCacheUpdateParent [%p]", this));
}

OfflineCacheUpdateParent::~OfflineCacheUpdateParent() {
  LOG(("OfflineCacheUpdateParent::~OfflineCacheUpdateParent [%p]", this));
}

void OfflineCacheUpdateParent::ActorDestroy(ActorDestroyReason why) {
  mIPCClosed = true;
}

nsresult OfflineCacheUpdateParent::Schedule(
    nsIURI* aManifestURI, nsIURI* aDocumentURI,
    const PrincipalInfo& aLoadingPrincipalInfo, const bool& stickDocument,
    const CookieSettingsArgs& aCookieSettingsArgs) {
  LOG(("OfflineCacheUpdateParent::RecvSchedule [%p]", this));

  nsresult rv;

  RefPtr<nsOfflineCacheUpdate> update;
  if (!aManifestURI) {
    return NS_ERROR_FAILURE;
  }

  mLoadingPrincipal = PrincipalInfoToPrincipal(aLoadingPrincipalInfo, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsOfflineCacheUpdateService* service =
      nsOfflineCacheUpdateService::EnsureService();
  if (!service) {
    return NS_ERROR_FAILURE;
  }

  bool offlinePermissionAllowed = false;

  rv = service->OfflineAppAllowed(mLoadingPrincipal, &offlinePermissionAllowed);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!offlinePermissionAllowed) {
    return NS_ERROR_DOM_SECURITY_ERR;
  }

  if (!aDocumentURI) {
    return NS_ERROR_FAILURE;
  }

  if (!NS_SecurityCompareURIs(aManifestURI, aDocumentURI, false)) {
    return NS_ERROR_DOM_SECURITY_ERR;
  }

  nsAutoCString originSuffix;
  rv = mLoadingPrincipal->GetOriginSuffix(originSuffix);
  NS_ENSURE_SUCCESS(rv, rv);

  service->FindUpdate(aManifestURI, originSuffix, nullptr,
                      getter_AddRefs(update));
  if (!update) {
    update = new nsOfflineCacheUpdate();

    // Leave aDocument argument null. Only glues and children keep
    // document instances.
    rv = update->Init(aManifestURI, aDocumentURI, mLoadingPrincipal, nullptr,
                      nullptr);
    NS_ENSURE_SUCCESS(rv, rv);

    update->SetCookieSettingsArgs(aCookieSettingsArgs);

    // Must add before Schedule() call otherwise we would miss
    // oncheck event notification.
    update->AddObserver(this, false);

    rv = update->Schedule();
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    update->AddObserver(this, false);
  }

  if (stickDocument) {
    update->StickDocument(aDocumentURI);
  }

  return NS_OK;
}

NS_IMETHODIMP
OfflineCacheUpdateParent::UpdateStateChanged(nsIOfflineCacheUpdate* aUpdate,
                                             uint32_t state) {
  if (mIPCClosed) {
    return NS_ERROR_UNEXPECTED;
  }

  LOG(("OfflineCacheUpdateParent::StateEvent [%p]", this));

  uint64_t byteProgress;
  aUpdate->GetByteProgress(&byteProgress);
  Unused << SendNotifyStateEvent(state, byteProgress);

  if (state == nsIOfflineCacheUpdateObserver::STATE_FINISHED) {
    // Tell the child the particulars after the update has finished.
    // Sending the Finish event will release the child side of the protocol
    // and notify "offline-cache-update-completed" on the child process.
    bool isUpgrade;
    aUpdate->GetIsUpgrade(&isUpgrade);
    bool succeeded;
    aUpdate->GetSucceeded(&succeeded);

    Unused << SendFinish(succeeded, isUpgrade);
  }

  return NS_OK;
}

NS_IMETHODIMP
OfflineCacheUpdateParent::ApplicationCacheAvailable(
    nsIApplicationCache* aApplicationCache) {
  if (mIPCClosed) {
    return NS_ERROR_UNEXPECTED;
  }

  NS_ENSURE_ARG(aApplicationCache);

  nsCString cacheClientId;
  aApplicationCache->GetClientID(cacheClientId);
  nsCString cacheGroupId;
  aApplicationCache->GetGroupID(cacheGroupId);

  Unused << SendAssociateDocuments(cacheGroupId, cacheClientId);
  return NS_OK;
}

//-----------------------------------------------------------------------------
// OfflineCacheUpdateParent::nsILoadContext
//-----------------------------------------------------------------------------

NS_IMETHODIMP
OfflineCacheUpdateParent::GetAssociatedWindow(
    mozIDOMWindowProxy** aAssociatedWindow) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
OfflineCacheUpdateParent::GetTopWindow(mozIDOMWindowProxy** aTopWindow) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
OfflineCacheUpdateParent::GetTopFrameElement(dom::Element** aElement) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
OfflineCacheUpdateParent::GetNestedFrameId(uint64_t* aId) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
OfflineCacheUpdateParent::GetIsContent(bool* aIsContent) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
OfflineCacheUpdateParent::GetUsePrivateBrowsing(bool* aUsePrivateBrowsing) {
  return NS_ERROR_NOT_IMPLEMENTED;
}
NS_IMETHODIMP
OfflineCacheUpdateParent::SetUsePrivateBrowsing(bool aUsePrivateBrowsing) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
OfflineCacheUpdateParent::SetPrivateBrowsing(bool aUsePrivateBrowsing) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
OfflineCacheUpdateParent::GetUseRemoteTabs(bool* aUseRemoteTabs) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
OfflineCacheUpdateParent::SetRemoteTabs(bool aUseRemoteTabs) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
OfflineCacheUpdateParent::GetScriptableOriginAttributes(
    JSContext* aCx, JS::MutableHandleValue aAttrs) {
  NS_ENSURE_TRUE(mLoadingPrincipal, NS_ERROR_UNEXPECTED);

  nsresult rv = mLoadingPrincipal->GetOriginAttributes(aCx, aAttrs);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP_(void)
OfflineCacheUpdateParent::GetOriginAttributes(
    mozilla::OriginAttributes& aAttrs) {
  if (mLoadingPrincipal) {
    aAttrs = mLoadingPrincipal->OriginAttributesRef();
  }
}

NS_IMETHODIMP
OfflineCacheUpdateParent::GetUseTrackingProtection(
    bool* aUseTrackingProtection) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
OfflineCacheUpdateParent::SetUseTrackingProtection(
    bool aUseTrackingProtection) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

}  // namespace docshell
}  // namespace mozilla
