/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_StorageAccess_h
#define mozilla_StorageAccess_h

#include <cstdint>

class nsIChannel;
class nsICookieSettings;
class nsIPrincipal;
class nsIURI;
class nsPIDOMWindowInner;

namespace mozilla {
namespace dom {
class Document;
}

// The order of these entries matters, as we use std::min for total ordering
// of permissions. Private Browsing is considered to be more limiting
// then session scoping
enum class StorageAccess {
  // The storage should be partitioned for third-party resources. if the
  // caller is unable to do it, deny the storage access.
  ePartitionForeignOrDeny = -2,
  // The storage should be partitioned for third-party trackers. if the caller
  // is unable to do it, deny the storage access.
  ePartitionTrackersOrDeny = -1,
  // Don't allow access to the storage
  eDeny = 0,
  // Allow access to the storage, but only if it is secure to do so in a
  // private browsing context.
  ePrivateBrowsing = 1,
  // Allow access to the storage, but only persist it for the current session
  eSessionScoped = 2,
  // Allow access to the storage
  eAllow = 3,
  // Keep this at the end.  Used for serialization, but not a valid value.
  eNumValues = 4,
};

/*
 * Checks if storage for the given window is permitted by a combination of
 * the user's preferences, and whether the window is a third-party iframe.
 *
 * This logic is intended to be shared between the different forms of
 * persistent storage which are available to web pages. Cookies don't use
 * this logic, and security logic related to them must be updated separately.
 */
StorageAccess StorageAllowedForWindow(nsPIDOMWindowInner* aWindow,
                                      uint32_t* aRejectedReason = nullptr);

/*
 * Checks if storage for the given document is permitted by a combination of
 * the user's preferences, and whether the document's window is a third-party
 * iframe.
 *
 * Note, this may be used on documents during the loading process where
 * the window's extant document has not been set yet.  The code in
 * StorageAllowedForWindow(), however, will not work in these cases.
 */
StorageAccess StorageAllowedForDocument(const dom::Document* aDoc);

/*
 * Checks if storage should be allowed for a new window with the given
 * principal, load URI, and parent.
 */
StorageAccess StorageAllowedForNewWindow(nsIPrincipal* aPrincipal, nsIURI* aURI,
                                         nsPIDOMWindowInner* aParent);

/*
 * Checks if storage should be allowed for the given channel.  The check will
 * be based on the channel result principal and, depending on preferences and
 * permissions, mozIThirdPartyUtil.isThirdPartyChannel().
 */
StorageAccess StorageAllowedForChannel(nsIChannel* aChannel);

/*
 * Checks if storage for the given principal is permitted by the user's
 * preferences. This method should be used only by ServiceWorker loading.
 */
StorageAccess StorageAllowedForServiceWorker(
    nsIPrincipal* aPrincipal, nsICookieSettings* aCookieSettings);

/*
 * Returns true if this window/channel/aPrincipal should disable storages
 * because of the anti-tracking feature.
 * Note that either aWindow or aChannel may be null when calling this
 * function. If the caller wants the UI to be notified when the storage gets
 * disabled, it must pass a non-null channel object.
 */
bool StorageDisabledByAntiTracking(nsPIDOMWindowInner* aWindow,
                                   nsIChannel* aChannel,
                                   nsIPrincipal* aPrincipal, nsIURI* aURI,
                                   uint32_t& aRejectedReason);

/*
 * Returns true if this document should disable storages because of the
 * anti-tracking feature.
 */
bool StorageDisabledByAntiTracking(dom::Document* aDocument, nsIURI* aURI);

bool ShouldPartitionStorage(StorageAccess aAccess);

bool ShouldPartitionStorage(uint32_t aRejectedReason);

bool StoragePartitioningEnabled(StorageAccess aAccess,
                                nsICookieSettings* aCookieSettings);

bool StoragePartitioningEnabled(uint32_t aRejectedReason,
                                nsICookieSettings* aCookieSettings);

}  // namespace mozilla

#endif  // mozilla_StorageAccess_h
