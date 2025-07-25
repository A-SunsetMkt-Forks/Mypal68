/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "StoragePrincipalHelper.h"

#include "mozilla/ipc/PBackgroundSharedTypes.h"
#include "mozilla/AntiTrackingCommon.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StorageAccess.h"
#include "nsIPrivateBrowsingChannel.h" //MY
#include "nsContentUtils.h"

namespace mozilla {

namespace {

bool ChooseOriginAttributes(nsIChannel* aChannel, OriginAttributes& aAttrs,
                            bool aForceInstrinsicStoragePrincipal) {
  MOZ_ASSERT(aChannel);

  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
  nsCOMPtr<nsICookieSettings> cs;
  if (NS_FAILED(loadInfo->GetCookieSettings(getter_AddRefs(cs)))) {
    return false;
  }

  if (!aForceInstrinsicStoragePrincipal) {
    nsCOMPtr<nsIURI> uri;
    nsresult rv = aChannel->GetURI(getter_AddRefs(uri));
    if (NS_FAILED(rv)) {
      return false;
    }

    uint32_t rejectedReason = 0;
    if (AntiTrackingCommon::IsFirstPartyStorageAccessGrantedFor(
            aChannel, uri, &rejectedReason)) {
      return false;
    }

    // Let's use the storage principal only if we need to partition the cookie
    // jar.  We use the lower-level AntiTrackingCommon API here to ensure this
    // check doesn't send notifications.
    if (!ShouldPartitionStorage(rejectedReason) ||
        !StoragePartitioningEnabled(rejectedReason, cs)) {
      return false;
    }
  }

  nsCOMPtr<nsIPrincipal> toplevelPrincipal = loadInfo->GetTopLevelPrincipal();
  if (!toplevelPrincipal) {
    return false;
  }
  // Cast to BasePrincipal to continue to get acess to GetUri()
  auto* basePrin = BasePrincipal::Cast(toplevelPrincipal);
  nsCOMPtr<nsIURI> principalURI;

  nsresult rv = basePrin->GetURI(getter_AddRefs(principalURI));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return false;
  }

  aAttrs.SetFirstPartyDomain(false, principalURI, true /* aForced */);
  return true;
}

}  // namespace

// static
nsresult StoragePrincipalHelper::Create(nsIChannel* aChannel,
                                        nsIPrincipal* aPrincipal,
                                        nsIPrincipal** aStoragePrincipal) {
  MOZ_ASSERT(aChannel);
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(aStoragePrincipal);

  auto scopeExit = MakeScopeExit([&] {
    nsCOMPtr<nsIPrincipal> storagePrincipal = aPrincipal;
    storagePrincipal.forget(aStoragePrincipal);
  });

  OriginAttributes attrs = aPrincipal->OriginAttributesRef();
  if (!ChooseOriginAttributes(aChannel, attrs, false)) {
    return NS_OK;
  }

  scopeExit.release();

  nsCOMPtr<nsIPrincipal> storagePrincipal =
      BasePrincipal::Cast(aPrincipal)->CloneForcingOriginAttributes(attrs);

  storagePrincipal.forget(aStoragePrincipal);
  return NS_OK;
}

// static
nsresult
StoragePrincipalHelper::PrepareEffectiveStoragePrincipalOriginAttributes(
    nsIChannel* aChannel, OriginAttributes& aOriginAttributes) {
  MOZ_ASSERT(aChannel);

  ChooseOriginAttributes(aChannel, aOriginAttributes, false);
  return NS_OK;
}

// static
bool StoragePrincipalHelper::VerifyValidStoragePrincipalInfoForPrincipalInfo(
    const mozilla::ipc::PrincipalInfo& aStoragePrincipalInfo,
    const mozilla::ipc::PrincipalInfo& aPrincipalInfo) {
  if (aStoragePrincipalInfo.type() != aPrincipalInfo.type()) {
    return false;
  }

  if (aStoragePrincipalInfo.type() ==
      mozilla::ipc::PrincipalInfo::TContentPrincipalInfo) {
    const mozilla::ipc::ContentPrincipalInfo& spInfo =
        aStoragePrincipalInfo.get_ContentPrincipalInfo();
    const mozilla::ipc::ContentPrincipalInfo& pInfo =
        aPrincipalInfo.get_ContentPrincipalInfo();

    if (!spInfo.attrs().EqualsIgnoringFPD(pInfo.attrs()) ||
        spInfo.originNoSuffix() != pInfo.originNoSuffix() ||
        spInfo.spec() != pInfo.spec() || spInfo.domain() != pInfo.domain() ||
        spInfo.baseDomain() != pInfo.baseDomain()) {
      return false;
    }

    return true;
  }

  if (aStoragePrincipalInfo.type() ==
      mozilla::ipc::PrincipalInfo::TSystemPrincipalInfo) {
    // Nothing to check here.
    return true;
  }

  if (aStoragePrincipalInfo.type() ==
      mozilla::ipc::PrincipalInfo::TNullPrincipalInfo) {
    const mozilla::ipc::NullPrincipalInfo& spInfo =
        aStoragePrincipalInfo.get_NullPrincipalInfo();
    const mozilla::ipc::NullPrincipalInfo& pInfo =
        aPrincipalInfo.get_NullPrincipalInfo();

    return spInfo.spec() == pInfo.spec() &&
           spInfo.attrs().EqualsIgnoringFPD(pInfo.attrs());
  }

  if (aStoragePrincipalInfo.type() ==
      mozilla::ipc::PrincipalInfo::TExpandedPrincipalInfo) {
    const mozilla::ipc::ExpandedPrincipalInfo& spInfo =
        aStoragePrincipalInfo.get_ExpandedPrincipalInfo();
    const mozilla::ipc::ExpandedPrincipalInfo& pInfo =
        aPrincipalInfo.get_ExpandedPrincipalInfo();

    if (!spInfo.attrs().EqualsIgnoringFPD(pInfo.attrs())) {
      return false;
    }

    if (spInfo.allowlist().Length() != pInfo.allowlist().Length()) {
      return false;
    }

    for (uint32_t i = 0; i < spInfo.allowlist().Length(); ++i) {
      if (!VerifyValidStoragePrincipalInfoForPrincipalInfo(
              spInfo.allowlist()[i], pInfo.allowlist()[i])) {
        return false;
      }
    }

    return true;
  }

  MOZ_CRASH("Invalid principalInfo type");
  return false;
}

// static
bool StoragePrincipalHelper::GetOriginAttributes(
    nsIChannel* aChannel, mozilla::OriginAttributes& aAttributes,
    StoragePrincipalHelper::PrincipalType aPrincipalType) {
  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
  loadInfo->GetOriginAttributes(&aAttributes);

  bool isPrivate = false;
  nsCOMPtr<nsIPrivateBrowsingChannel> pbChannel = do_QueryInterface(aChannel);
  if (pbChannel) {
    nsresult rv = pbChannel->GetIsChannelPrivate(&isPrivate);
    NS_ENSURE_SUCCESS(rv, false);
  } else {
    // Some channels may not implement nsIPrivateBrowsingChannel
    nsCOMPtr<nsILoadContext> loadContext;
    NS_QueryNotificationCallbacks(aChannel, loadContext);
    isPrivate = loadContext && loadContext->UsePrivateBrowsing();
  }
  aAttributes.SyncAttributesWithPrivateBrowsing(isPrivate);

  switch (aPrincipalType) {
    case eRegularPrincipal:
      break;

    case eStorageAccessPrincipal:
      PrepareEffectiveStoragePrincipalOriginAttributes(aChannel, aAttributes);
      break;

    case ePartitionedPrincipal:
      ChooseOriginAttributes(aChannel, aAttributes, true);
      break;
  }

  return true;
}

// static
bool StoragePrincipalHelper::GetRegularPrincipalOriginAttributes(
    Document* aDocument, OriginAttributes& aAttributes) {
  aAttributes = mozilla::OriginAttributes();
  if (!aDocument) {
    return false;
  }

  nsCOMPtr<nsILoadGroup> loadGroup = aDocument->GetDocumentLoadGroup();
  if (loadGroup) {
    return GetRegularPrincipalOriginAttributes(loadGroup, aAttributes);
  }

  nsCOMPtr<nsIChannel> channel = aDocument->GetChannel();
  if (!channel) {
    return false;
  }

  return GetOriginAttributes(channel, aAttributes, eRegularPrincipal);
}

// static
bool StoragePrincipalHelper::GetRegularPrincipalOriginAttributes(
    nsILoadGroup* aLoadGroup, OriginAttributes& aAttributes) {
  aAttributes = mozilla::OriginAttributes();
  if (!aLoadGroup) {
    return false;
  }

  nsCOMPtr<nsIInterfaceRequestor> callbacks;
  aLoadGroup->GetNotificationCallbacks(getter_AddRefs(callbacks));
  if (!callbacks) {
    return false;
  }

  nsCOMPtr<nsILoadContext> loadContext = do_GetInterface(callbacks);
  if (!loadContext) {
    return false;
  }

  loadContext->GetOriginAttributes(aAttributes);
  return true;
}

}  // namespace mozilla
