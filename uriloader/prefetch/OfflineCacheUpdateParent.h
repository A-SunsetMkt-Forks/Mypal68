/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsOfflineCacheUpdateParent_h
#define nsOfflineCacheUpdateParent_h

#include "mozilla/docshell/POfflineCacheUpdateParent.h"
#include "mozilla/BasePrincipal.h"
#include "nsIOfflineCacheUpdate.h"

#include "nsCOMPtr.h"
#include "nsString.h"
#include "nsILoadContext.h"

class nsIPrincipal;

namespace mozilla {

namespace ipc {
class URIParams;
}  // namespace ipc

namespace net {
class CookieSettingsArgs;
}

namespace docshell {

class OfflineCacheUpdateParent : public POfflineCacheUpdateParent,
                                 public nsIOfflineCacheUpdateObserver,
                                 public nsILoadContext {
  typedef mozilla::ipc::URIParams URIParams;
  typedef mozilla::ipc::PrincipalInfo PrincipalInfo;

 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOFFLINECACHEUPDATEOBSERVER
  NS_DECL_NSILOADCONTEXT

  nsresult Schedule(nsIURI* manifestURI, nsIURI* documentURI,
                    const PrincipalInfo& loadingPrincipalInfo,
                    const bool& stickDocument,
                    const net::CookieSettingsArgs& aCookieSettingsArgs);

  void StopSendingMessagesToChild() { mIPCClosed = true; }

  explicit OfflineCacheUpdateParent();

  virtual void ActorDestroy(ActorDestroyReason aWhy) override;

 private:
  ~OfflineCacheUpdateParent();

  bool mIPCClosed;

  nsCOMPtr<nsIPrincipal> mLoadingPrincipal;
};

}  // namespace docshell
}  // namespace mozilla

#endif
