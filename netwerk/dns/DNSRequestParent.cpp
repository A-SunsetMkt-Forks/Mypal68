/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/net/DNSRequestParent.h"
#include "nsIDNSService.h"
#include "nsNetCID.h"
#include "nsThreadUtils.h"
#include "nsICancelable.h"
#include "nsIDNSRecord.h"
#include "nsHostResolver.h"
#include "mozilla/Unused.h"

using namespace mozilla::ipc;

namespace mozilla {
namespace net {

DNSRequestParent::DNSRequestParent() : mFlags(0), mIPCClosed(false) {}

void DNSRequestParent::DoAsyncResolve(const nsACString& hostname,
                                      const OriginAttributes& originAttributes,
                                      uint32_t flags) {
  nsresult rv;
  mFlags = flags;
  nsCOMPtr<nsIDNSService> dns = do_GetService(NS_DNSSERVICE_CONTRACTID, &rv);
  if (NS_SUCCEEDED(rv)) {
    nsCOMPtr<nsIEventTarget> main = GetMainThreadEventTarget();
    nsCOMPtr<nsICancelable> unused;
    rv = dns->AsyncResolveNative(hostname, flags, this, main, originAttributes,
                                 getter_AddRefs(unused));
  }

  if (NS_FAILED(rv) && !mIPCClosed) {
    mIPCClosed = true;
    Unused << SendLookupCompleted(DNSRequestResponse(rv));
  }
}

mozilla::ipc::IPCResult DNSRequestParent::RecvCancelDNSRequest(
    const nsCString& hostName, const uint16_t& type,
    const OriginAttributes& originAttributes, const uint32_t& flags,
    const nsresult& reason) {
  nsresult rv;
  nsCOMPtr<nsIDNSService> dns = do_GetService(NS_DNSSERVICE_CONTRACTID, &rv);
  if (NS_SUCCEEDED(rv)) {
    if (type == nsIDNSService::RESOLVE_TYPE_DEFAULT) {
      rv = dns->CancelAsyncResolveNative(hostName, flags, this, reason,
                                         originAttributes);
    } else {
      rv = dns->CancelAsyncResolveByTypeNative(hostName, type, flags, this,
                                               reason, originAttributes);
    }
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult DNSRequestParent::Recv__delete__() {
  mIPCClosed = true;
  return IPC_OK();
}

void DNSRequestParent::ActorDestroy(ActorDestroyReason why) {
  // We may still have refcount>0 if DNS hasn't called our OnLookupComplete
  // yet, but child process has crashed.  We must not send any more msgs
  // to child, or IPDL will kill chrome process, too.
  mIPCClosed = true;
}
//-----------------------------------------------------------------------------
// DNSRequestParent::nsISupports
//-----------------------------------------------------------------------------

NS_IMPL_ISUPPORTS(DNSRequestParent, nsIDNSListener)

//-----------------------------------------------------------------------------
// nsIDNSListener functions
//-----------------------------------------------------------------------------

NS_IMETHODIMP
DNSRequestParent::OnLookupComplete(nsICancelable* request, nsIDNSRecord* rec,
                                   nsresult status) {
  if (mIPCClosed) {
    // nothing to do: child probably crashed
    return NS_OK;
  }

  if (NS_SUCCEEDED(status)) {
    MOZ_ASSERT(rec);

    nsAutoCString cname;
    if (mFlags & nsHostResolver::RES_CANON_NAME) {
      rec->GetCanonicalName(cname);
    }

    // Get IP addresses for hostname (use port 80 as dummy value for NetAddr)
    NetAddrArray array;
    NetAddr addr;
    while (NS_SUCCEEDED(rec->GetNextAddr(80, &addr))) {
      array.AppendElement(addr);
    }

    Unused << SendLookupCompleted(DNSRequestResponse(DNSRecord(cname, array)));
  } else {
    Unused << SendLookupCompleted(DNSRequestResponse(status));
  }

  mIPCClosed = true;
  return NS_OK;
}

NS_IMETHODIMP
DNSRequestParent::OnLookupByTypeComplete(nsICancelable* aRequest,
                                         nsIDNSByTypeRecord* aRes,
                                         nsresult aStatus) {
  if (mIPCClosed) {
    // nothing to do: child probably crashed
    return NS_OK;
  }

  if (NS_SUCCEEDED(aStatus)) {
    nsTArray<nsCString> rec;
    aRes->GetRecords(rec);
    Unused << SendLookupCompleted(DNSRequestResponse(rec));
  } else {
    Unused << SendLookupCompleted(DNSRequestResponse(aStatus));
  }
  mIPCClosed = true;
  return NS_OK;
}

}  // namespace net
}  // namespace mozilla
