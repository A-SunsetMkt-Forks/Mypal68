/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Logging.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_security.h"
#include "nsString.h"
#include "nsCOMPtr.h"
#include "nsIURI.h"
#include "nsIPrincipal.h"
#include "nsIContent.h"
#include "nsCSPService.h"
#include "nsIContentSecurityPolicy.h"
#include "nsError.h"
#include "nsIAsyncVerifyRedirectCallback.h"
#include "nsAsyncRedirectVerifyHelper.h"
#include "nsContentUtils.h"
#include "nsContentPolicyUtils.h"
#include "nsNetUtil.h"

using namespace mozilla;

static LazyLogModule gCspPRLog("CSP");

CSPService::CSPService() = default;

CSPService::~CSPService() = default;

NS_IMPL_ISUPPORTS(CSPService, nsIContentPolicy, nsIChannelEventSink)

// Helper function to identify protocols and content types not subject to CSP.
bool subjectToCSP(nsIURI* aURI, nsContentPolicyType aContentType) {
  nsContentPolicyType contentType =
      nsContentUtils::InternalContentPolicyTypeToExternal(aContentType);

  // These content types are not subject to CSP content policy checks:
  // TYPE_CSP_REPORT -- csp can't block csp reports
  // TYPE_REFRESH    -- never passed to ShouldLoad (see nsIContentPolicy.idl)
  // TYPE_DOCUMENT   -- used for frame-ancestors
  if (contentType == nsIContentPolicy::TYPE_CSP_REPORT ||
      contentType == nsIContentPolicy::TYPE_REFRESH ||
      contentType == nsIContentPolicy::TYPE_DOCUMENT) {
    return false;
  }

  // The three protocols: data:, blob: and filesystem: share the same
  // protocol flag (URI_IS_LOCAL_RESOURCE) with other protocols,
  // but those three protocols get special attention in CSP and
  // are subject to CSP, hence we have to make sure those
  // protocols are subject to CSP, see:
  // http://www.w3.org/TR/CSP2/#source-list-guid-matching
  if (aURI->SchemeIs("data") || aURI->SchemeIs("blob") ||
      aURI->SchemeIs("filesystem")) {
    return true;
  }

  // Finally we have to allowlist "about:" which does not fall into
  // the category underneath and also "javascript:" which is not
  // subject to CSP content loading rules.
  if (aURI->SchemeIs("about") || aURI->SchemeIs("javascript")) {
    return false;
  }

  // Please note that it should be possible for websites to
  // allowlist their own protocol handlers with respect to CSP,
  // hence we use protocol flags to accomplish that, but we also
  // want resource:, chrome: and moz-icon to be subject to CSP
  // (which also use URI_IS_LOCAL_RESOURCE).
  // Exception to the rule are images, styles, and localization
  // DTDs using a scheme of resource: or chrome:
  bool isImgOrStyleOrDTD = contentType == nsIContentPolicy::TYPE_IMAGE ||
                           contentType == nsIContentPolicy::TYPE_STYLESHEET ||
                           contentType == nsIContentPolicy::TYPE_DTD;
  if (aURI->SchemeIs("resource")) {
    nsAutoCString uriSpec;
    aURI->GetSpec(uriSpec);
    // Exempt pdf.js from being subject to a page's CSP.
    if (StringBeginsWith(uriSpec, "resource://pdf.js/"_ns)) {
      return false;
    }
    if (!isImgOrStyleOrDTD) {
      return true;
    }
  }
  if (aURI->SchemeIs("chrome") && !isImgOrStyleOrDTD) {
    return true;
  }
  if (aURI->SchemeIs("moz-icon")) {
    return true;
  }
  bool match;
  nsresult rv = NS_URIChainHasFlags(
      aURI, nsIProtocolHandler::URI_IS_LOCAL_RESOURCE, &match);
  if (NS_SUCCEEDED(rv) && match) {
    return false;
  }
  // all other protocols are subject To CSP.
  return true;
}

/* static */ nsresult CSPService::ConsultCSP(nsIURI* aContentLocation,
                                             nsILoadInfo* aLoadInfo,
                                             const nsACString& aMimeTypeGuess,
                                             int16_t* aDecision) {
  if (!aContentLocation) {
    return NS_ERROR_FAILURE;
  }

  uint32_t contentType = aLoadInfo->InternalContentPolicyType();
  nsCOMPtr<nsISupports> requestContext = aLoadInfo->GetLoadingContext();

  nsCOMPtr<nsICSPEventListener> cspEventListener;
  nsresult rv =
      aLoadInfo->GetCspEventListener(getter_AddRefs(cspEventListener));
  NS_ENSURE_SUCCESS(rv, rv);

  if (MOZ_LOG_TEST(gCspPRLog, LogLevel::Debug)) {
    MOZ_LOG(gCspPRLog, LogLevel::Debug,
            ("CSPService::ShouldLoad called for %s",
             aContentLocation->GetSpecOrDefault().get()));
  }

  // default decision, CSP can revise it if there's a policy to enforce
  *aDecision = nsIContentPolicy::ACCEPT;

  // No need to continue processing if CSP is disabled or if the protocol
  // or type is *not* subject to CSP.
  // Please note, the correct way to opt-out of CSP using a custom
  // protocolHandler is to set one of the nsIProtocolHandler flags
  // that are allowlistet in subjectToCSP()
  if (!StaticPrefs::security_csp_enable() ||
      !subjectToCSP(aContentLocation, contentType)) {
    return NS_OK;
  }

  nsAutoString cspNonce;
  rv = aLoadInfo->GetCspNonce(cspNonce);
  NS_ENSURE_SUCCESS(rv, rv);

  // 1) Apply speculate CSP for preloads
  bool isPreload = nsContentUtils::IsPreloadType(contentType);

  if (isPreload) {
    nsCOMPtr<nsIContentSecurityPolicy> preloadCsp = aLoadInfo->GetPreloadCsp();
    if (preloadCsp) {
      // obtain the enforcement decision
      rv = preloadCsp->ShouldLoad(
          contentType, cspEventListener, aContentLocation, requestContext,
          nullptr,  // no redirect, aOriginal URL is null.
          false, cspNonce, aDecision);
      NS_ENSURE_SUCCESS(rv, rv);

      // if the preload policy already denied the load, then there
      // is no point in checking the real policy
      if (NS_CP_REJECTED(*aDecision)) {
        NS_SetRequestBlockingReason(
            aLoadInfo, nsILoadInfo::BLOCKING_REASON_CONTENT_POLICY_PRELOAD);

        return NS_OK;
      }
    }
  }

  // 2) Apply actual CSP to all loads. Please note that in case
  // the csp should be overruled (e.g. by an ExpandedPrincipal)
  // then loadinfo->GetCSP() returns that CSP instead of the
  // document's CSP.
  nsCOMPtr<nsIContentSecurityPolicy> csp = aLoadInfo->GetCsp();

  if (csp) {
    // obtain the enforcement decision
    rv = csp->ShouldLoad(contentType, cspEventListener, aContentLocation,
                         requestContext,
                         nullptr,  // no redirect, aOriginal URL is null.
                         !isPreload && aLoadInfo->GetSendCSPViolationEvents(),
                         cspNonce, aDecision);

    if (NS_CP_REJECTED(*aDecision)) {
      NS_SetRequestBlockingReason(
          aLoadInfo, nsILoadInfo::BLOCKING_REASON_CONTENT_POLICY_GENERAL);
    }

    NS_ENSURE_SUCCESS(rv, rv);
  }
  return NS_OK;
}

/* nsIContentPolicy implementation */
NS_IMETHODIMP
CSPService::ShouldLoad(nsIURI* aContentLocation, nsILoadInfo* aLoadInfo,
                       const nsACString& aMimeTypeGuess, int16_t* aDecision) {
  return ConsultCSP(aContentLocation, aLoadInfo, aMimeTypeGuess, aDecision);
}

NS_IMETHODIMP
CSPService::ShouldProcess(nsIURI* aContentLocation, nsILoadInfo* aLoadInfo,
                          const nsACString& aMimeTypeGuess,
                          int16_t* aDecision) {
  if (!aContentLocation) {
    return NS_ERROR_FAILURE;
  }
  uint32_t contentType = aLoadInfo->InternalContentPolicyType();

  if (MOZ_LOG_TEST(gCspPRLog, LogLevel::Debug)) {
    MOZ_LOG(gCspPRLog, LogLevel::Debug,
            ("CSPService::ShouldProcess called for %s",
             aContentLocation->GetSpecOrDefault().get()));
  }

  // ShouldProcess is only relevant to TYPE_OBJECT, so let's convert the
  // internal contentPolicyType to the mapping external one.
  // If it is not TYPE_OBJECT, we can return at this point.
  // Note that we should still pass the internal contentPolicyType
  // (contentType) to ShouldLoad().
  uint32_t policyType =
      nsContentUtils::InternalContentPolicyTypeToExternal(contentType);

  if (policyType != nsIContentPolicy::TYPE_OBJECT) {
    *aDecision = nsIContentPolicy::ACCEPT;
    return NS_OK;
  }

  return ShouldLoad(aContentLocation, aLoadInfo, aMimeTypeGuess, aDecision);
}

/* nsIChannelEventSink implementation */
NS_IMETHODIMP
CSPService::AsyncOnChannelRedirect(nsIChannel* oldChannel,
                                   nsIChannel* newChannel, uint32_t flags,
                                   nsIAsyncVerifyRedirectCallback* callback) {
  net::nsAsyncRedirectAutoCallback autoCallback(callback);

  if (XRE_IsE10sParentProcess()) {
    nsCOMPtr<nsIParentChannel> parentChannel;
    NS_QueryNotificationCallbacks(oldChannel, parentChannel);
    // Since this is an IPC'd channel we do not have access to the request
    // context. In turn, we do not have an event target for policy violations.
    // Enforce the CSP check in the content process where we have that info.
    if (parentChannel) {
      return NS_OK;
    }
  }

  nsCOMPtr<nsIURI> newUri;
  nsresult rv = newChannel->GetURI(getter_AddRefs(newUri));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsILoadInfo> loadInfo = oldChannel->LoadInfo();
  nsCOMPtr<nsICSPEventListener> cspEventListener;
  rv = loadInfo->GetCspEventListener(getter_AddRefs(cspEventListener));
  NS_ENSURE_SUCCESS(rv, rv);

  // No need to continue processing if CSP is disabled or if the protocol
  // is *not* subject to CSP.
  // Please note, the correct way to opt-out of CSP using a custom
  // protocolHandler is to set one of the nsIProtocolHandler flags
  // that are whitelistet in subjectToCSP()
  nsContentPolicyType policyType = loadInfo->InternalContentPolicyType();
  if (!StaticPrefs::security_csp_enable() ||
      !subjectToCSP(newUri, policyType)) {
    return NS_OK;
  }

  /* Since redirecting channels don't call into nsIContentPolicy, we call our
   * Content Policy implementation directly when redirects occur using the
   * information set in the LoadInfo when channels are created.
   *
   * We check if the CSP permits this host for this type of load, if not,
   * we cancel the load now.
   */
  nsCOMPtr<nsIURI> originalUri;
  rv = oldChannel->GetOriginalURI(getter_AddRefs(originalUri));
  if (NS_FAILED(rv)) {
    autoCallback.DontCallback();
    oldChannel->Cancel(NS_ERROR_DOM_BAD_URI);
    return rv;
  }

  nsAutoString cspNonce;
  rv = loadInfo->GetCspNonce(cspNonce);
  NS_ENSURE_SUCCESS(rv, rv);

  bool isPreload = nsContentUtils::IsPreloadType(policyType);

  /* On redirect, if the content policy is a preload type, rejecting the
   * preload results in the load silently failing, so we pass true to
   * the aSendViolationReports parameter. See Bug 1219453.
   */

  int16_t aDecision = nsIContentPolicy::ACCEPT;
  nsCOMPtr<nsISupports> requestContext = loadInfo->GetLoadingContext();

  // 1) Apply speculative CSP for preloads
  if (isPreload) {
    nsCOMPtr<nsIContentSecurityPolicy> preloadCsp = loadInfo->GetPreloadCsp();
    if (preloadCsp) {
      // Pass  originalURI to indicate the redirect
      preloadCsp->ShouldLoad(
          policyType,  // load type per nsIContentPolicy (uint32_t)
          cspEventListener,
          newUri,          // nsIURI
          requestContext,  // nsISupports
          originalUri,     // Original nsIURI
          true,            // aSendViolationReports
          cspNonce,        // nonce
          &aDecision);

      // if the preload policy already denied the load, then there
      // is no point in checking the real policy
      if (NS_CP_REJECTED(aDecision)) {
        autoCallback.DontCallback();
        oldChannel->Cancel(NS_ERROR_DOM_BAD_URI);
        return NS_BINDING_FAILED;
      }
    }
  }

  // 2) Apply actual CSP to all loads
  nsCOMPtr<nsIContentSecurityPolicy> csp = loadInfo->GetCsp();
  if (csp) {
    // Pass  originalURI to indicate the redirect
    csp->ShouldLoad(policyType,  // load type per nsIContentPolicy (uint32_t)
                    cspEventListener,
                    newUri,          // nsIURI
                    requestContext,  // nsISupports
                    originalUri,     // Original nsIURI
                    true,            // aSendViolationReports
                    cspNonce,        // nonce
                    &aDecision);
  }

  // if ShouldLoad doesn't accept the load, cancel the request
  if (!NS_CP_ACCEPTED(aDecision)) {
    autoCallback.DontCallback();
    oldChannel->Cancel(NS_ERROR_DOM_BAD_URI);
    return NS_BINDING_FAILED;
  }
  return NS_OK;
}
