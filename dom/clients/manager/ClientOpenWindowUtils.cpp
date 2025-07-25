/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ClientOpenWindowUtils.h"

#include "ClientInfo.h"
#include "ClientState.h"
#include "mozilla/dom/ScriptSettings.h" //MY
#include "mozilla/SystemGroup.h"
#include "nsContentUtils.h"
#include "nsFocusManager.h"
#include "nsIBrowserDOMWindow.h"
#include "nsIDocShell.h"
#include "nsIDOMChromeWindow.h"
#include "nsIURI.h"
#include "nsIWebProgress.h"
#include "nsIWebProgressListener.h"
#include "nsIWindowWatcher.h"
#include "nsIXPConnect.h"
#include "nsNetUtil.h"
#include "nsPIDOMWindow.h"
#include "nsPIWindowWatcher.h"
#include "nsPrintfCString.h"

#include "mozilla/dom/nsCSPContext.h"

#ifdef MOZ_WIDGET_ANDROID
#  include "GeneratedJNIWrappers.h"
#endif

namespace mozilla {
namespace dom {

namespace {

class WebProgressListener final : public nsIWebProgressListener,
                                  public nsSupportsWeakReference {
 public:
  NS_DECL_ISUPPORTS

  WebProgressListener(nsPIDOMWindowOuter* aWindow, nsIURI* aBaseURI,
                      already_AddRefed<ClientOpPromise::Private> aPromise)
      : mPromise(aPromise), mWindow(aWindow), mBaseURI(aBaseURI) {
    MOZ_ASSERT(aWindow);
    MOZ_ASSERT(aBaseURI);
    MOZ_ASSERT(NS_IsMainThread());
  }

  NS_IMETHOD
  OnStateChange(nsIWebProgress* aWebProgress, nsIRequest* aRequest,
                uint32_t aStateFlags, nsresult aStatus) override {
    if (!(aStateFlags & STATE_IS_DOCUMENT) ||
        !(aStateFlags & (STATE_STOP | STATE_TRANSFERRING))) {
      return NS_OK;
    }

    // Our caller keeps a strong reference, so it is safe to remove the listener
    // from ServiceWorkerPrivate.
    aWebProgress->RemoveProgressListener(this);

    nsCOMPtr<Document> doc = mWindow->GetExtantDoc();
    if (NS_WARN_IF(!doc)) {
      CopyableErrorResult rv;
      rv.ThrowInvalidStateError("Document is discarded");
      mPromise->Reject(rv, __func__);
      mPromise = nullptr;
      return NS_OK;
    }

    // Check same origin.
    nsCOMPtr<nsIScriptSecurityManager> securityManager =
        nsContentUtils::GetSecurityManager();
    bool isPrivateWin =
        doc->NodePrincipal()->OriginAttributesRef().mPrivateBrowsingId > 0;
    nsresult rv = securityManager->CheckSameOriginURI(
        doc->GetOriginalURI(), mBaseURI, false, isPrivateWin);
    if (NS_FAILED(rv)) {
      mPromise->Resolve(CopyableErrorResult(), __func__);
      mPromise = nullptr;
      return NS_OK;
    }

    Maybe<ClientInfo> info(doc->GetClientInfo());
    Maybe<ClientState> state(doc->GetClientState());

    if (NS_WARN_IF(info.isNothing() || state.isNothing())) {
      // XXXbz Can we find a useful error message here?
      CopyableErrorResult rv;
      rv.Throw(NS_ERROR_FAILURE);
      mPromise->Reject(rv, __func__);
      mPromise = nullptr;
      return NS_OK;
    }

    mPromise->Resolve(
        ClientInfoAndState(info.ref().ToIPC(), state.ref().ToIPC()), __func__);
    mPromise = nullptr;

    return NS_OK;
  }

  NS_IMETHOD
  OnProgressChange(nsIWebProgress* aWebProgress, nsIRequest* aRequest,
                   int32_t aCurSelfProgress, int32_t aMaxSelfProgress,
                   int32_t aCurTotalProgress,
                   int32_t aMaxTotalProgress) override {
    MOZ_ASSERT(false, "Unexpected notification.");
    return NS_OK;
  }

  NS_IMETHOD
  OnLocationChange(nsIWebProgress* aWebProgress, nsIRequest* aRequest,
                   nsIURI* aLocation, uint32_t aFlags) override {
    MOZ_ASSERT(false, "Unexpected notification.");
    return NS_OK;
  }

  NS_IMETHOD
  OnStatusChange(nsIWebProgress* aWebProgress, nsIRequest* aRequest,
                 nsresult aStatus, const char16_t* aMessage) override {
    MOZ_ASSERT(false, "Unexpected notification.");
    return NS_OK;
  }

  NS_IMETHOD
  OnSecurityChange(nsIWebProgress* aWebProgress, nsIRequest* aRequest,
                   uint32_t aState) override {
    MOZ_ASSERT(false, "Unexpected notification.");
    return NS_OK;
  }

  NS_IMETHOD
  OnContentBlockingEvent(nsIWebProgress* aWebProgress, nsIRequest* aRequest,
                         uint32_t aEvent) override {
    MOZ_ASSERT(false, "Unexpected notification.");
    return NS_OK;
  }

 private:
  ~WebProgressListener() {
    if (mPromise) {
      CopyableErrorResult rv;
      rv.ThrowAbortError("openWindow aborted");
      mPromise->Reject(rv, __func__);
      mPromise = nullptr;
    }
  }

  RefPtr<ClientOpPromise::Private> mPromise;
  // TODO: make window a weak ref and stop cycle collecting
  nsCOMPtr<nsPIDOMWindowOuter> mWindow;
  nsCOMPtr<nsIURI> mBaseURI;
};

NS_IMPL_ISUPPORTS(WebProgressListener, nsIWebProgressListener,
                  nsISupportsWeakReference);

void OpenWindow(const ClientOpenWindowArgs& aArgs, nsPIDOMWindowOuter** aWindow,
                ErrorResult& aRv) {
  MOZ_DIAGNOSTIC_ASSERT(aWindow);

  // [[1. Let url be the result of parsing url with entry settings object's API
  //   base URL.]]
  nsCOMPtr<nsIURI> uri;

  nsCOMPtr<nsIURI> baseURI;
  nsresult rv = NS_NewURI(getter_AddRefs(baseURI), aArgs.baseURL());
  if (NS_WARN_IF(NS_FAILED(rv))) {
    nsPrintfCString err("Invalid base URL \"%s\"", aArgs.baseURL().get());
    aRv.ThrowTypeError(err);
    return;
  }

  rv = NS_NewURI(getter_AddRefs(uri), aArgs.url(), nullptr, baseURI);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    nsPrintfCString err("Invalid URL \"%s\"", aArgs.url().get());
    aRv.ThrowTypeError(err);
    return;
  }

  nsCOMPtr<nsIPrincipal> principal =
      PrincipalInfoToPrincipal(aArgs.principalInfo());
  MOZ_DIAGNOSTIC_ASSERT(principal);

  nsCOMPtr<nsIContentSecurityPolicy> csp;
  if (aArgs.cspInfo().isSome()) {
    csp = CSPInfoToCSP(aArgs.cspInfo().ref(), nullptr);
  }

  // [[6.1 Open Window]]
  if (XRE_IsContentProcess()) {
    // Let's create a sandbox in order to have a valid JSContext and correctly
    // propagate the SubjectPrincipal.
    AutoJSAPI jsapi;
    jsapi.Init();

    JSContext* cx = jsapi.cx();

    nsIXPConnect* xpc = nsContentUtils::XPConnect();
    MOZ_DIAGNOSTIC_ASSERT(xpc);

    JS::Rooted<JSObject*> sandbox(cx);
    rv = xpc->CreateSandbox(cx, principal, sandbox.address());
    if (NS_WARN_IF(NS_FAILED(rv))) {
      aRv.ThrowTypeError("Unable to open window");
      return;
    }

    JSAutoRealm ar(cx, sandbox);

    // ContentProcess
    nsCOMPtr<nsIWindowWatcher> wwatch =
        do_GetService(NS_WINDOWWATCHER_CONTRACTID, &rv);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      aRv.ThrowTypeError("Unable to open window");
      return;
    }
    nsCOMPtr<nsPIWindowWatcher> pwwatch(do_QueryInterface(wwatch));
    if (NS_WARN_IF(!pwwatch)) {
      aRv.ThrowTypeError("Unable to open window");
      return;
    }

    nsCString spec;
    rv = uri->GetSpec(spec);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      aRv.ThrowTypeError("Unable to open window");
      return;
    }

    nsCOMPtr<mozIDOMWindowProxy> newWindow;
    rv = pwwatch->OpenWindow2(
        nullptr, spec.get(), nullptr, nullptr, false, false, true, nullptr,
        // Not a spammy popup; we got permission, we swear!
        /* aIsPopupSpam = */ false,
        // Don't force noopener.  We're not passing in an
        // opener anyway, and we _do_ want the returned
        // window.
        /* aForceNoOpener = */ false,
        /* aForceNoReferrer = */ false,
        /* aLoadInfp = */ nullptr, getter_AddRefs(newWindow));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      aRv.ThrowTypeError("Unable to open window");
      return;
    }
    nsCOMPtr<nsPIDOMWindowOuter> pwindow = nsPIDOMWindowOuter::From(newWindow);
    pwindow.forget(aWindow);
    MOZ_DIAGNOSTIC_ASSERT(*aWindow);
    return;
  }

  // Find the most recent browser window and open a new tab in it.
  nsCOMPtr<nsPIDOMWindowOuter> browserWindow =
      nsContentUtils::GetMostRecentNonPBWindow();
  if (!browserWindow) {
    // It is possible to be running without a browser window on Mac OS, so
    // we need to open a new chrome window.
    // TODO(catalinb): open new chrome window. Bug 1218080
    aRv.ThrowTypeError("Unable to open window");
    return;
  }

  nsCOMPtr<nsIDOMChromeWindow> chromeWin = do_QueryInterface(browserWindow);
  if (NS_WARN_IF(!chromeWin)) {
    // XXXbz Can this actually happen?  Seems unlikely.
    aRv.ThrowTypeError("Unable to open window");
    return;
  }

  nsCOMPtr<nsIBrowserDOMWindow> bwin;
  chromeWin->GetBrowserDOMWindow(getter_AddRefs(bwin));

  if (NS_WARN_IF(!bwin)) {
    aRv.ThrowTypeError("Unable to open window");
    return;
  }

  nsCOMPtr<mozIDOMWindowProxy> win;
  rv = bwin->OpenURI(uri, nullptr, nsIBrowserDOMWindow::OPEN_DEFAULTWINDOW,
                     nsIBrowserDOMWindow::OPEN_NEW, principal, csp,
                     getter_AddRefs(win));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aRv.ThrowTypeError("Unable to open window");
    return;
  }

  nsCOMPtr<nsPIDOMWindowOuter> pWin = nsPIDOMWindowOuter::From(win);
  pWin.forget(aWindow);
  MOZ_DIAGNOSTIC_ASSERT(*aWindow);

  return;
}

void WaitForLoad(const ClientOpenWindowArgs& aArgs,
                 nsPIDOMWindowOuter* aOuterWindow,
                 ClientOpPromise::Private* aPromise) {
  MOZ_DIAGNOSTIC_ASSERT(aOuterWindow);

  RefPtr<ClientOpPromise::Private> promise = aPromise;

  nsFocusManager::FocusWindow(aOuterWindow);

  nsCOMPtr<nsIURI> baseURI;
  nsresult rv = NS_NewURI(getter_AddRefs(baseURI), aArgs.baseURL());
  if (NS_WARN_IF(NS_FAILED(rv))) {
    // Shouldn't really happen, since we passed in the serialization of a URI.
    CopyableErrorResult result;
    result.ThrowSyntaxError("Bad URL");
    promise->Reject(result, __func__);
    return;
  }

  nsCOMPtr<nsIDocShell> docShell = aOuterWindow->GetDocShell();
  nsCOMPtr<nsIWebProgress> webProgress = do_GetInterface(docShell);

  if (NS_WARN_IF(!webProgress)) {
    CopyableErrorResult rv;
    rv.ThrowInvalidStateError("No browsing context");
    promise->Reject(rv, __func__);
    return;
  }

  RefPtr<ClientOpPromise> ref = promise;

  RefPtr<WebProgressListener> listener =
      new WebProgressListener(aOuterWindow, baseURI, promise.forget());

  rv = webProgress->AddProgressListener(listener,
                                        nsIWebProgress::NOTIFY_STATE_DOCUMENT);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    CopyableErrorResult result;
    // XXXbz Can we throw something better here?
    result.Throw(rv);
    promise->Reject(result, __func__);
    return;
  }

  // Hold the listener alive until the promise settles
  ref->Then(
      aOuterWindow->EventTargetFor(TaskCategory::Other), __func__,
      [listener](const ClientOpResult& aResult) {},
      [listener](const CopyableErrorResult& aResult) {});
}

#ifdef MOZ_WIDGET_ANDROID

class LaunchObserver final : public nsIObserver {
  RefPtr<GenericPromise::Private> mPromise;

  LaunchObserver() : mPromise(new GenericPromise::Private(__func__)) {}

  ~LaunchObserver() = default;

  NS_IMETHOD
  Observe(nsISupports* aSubject, const char* aTopic,
          const char16_t* aData) override {
    nsCOMPtr<nsIObserverService> os = services::GetObserverService();
    if (os) {
      os->RemoveObserver(this, "BrowserChrome:Ready");
    }
    mPromise->Resolve(true, __func__);
    return NS_OK;
  }

 public:
  static already_AddRefed<LaunchObserver> Create() {
    nsCOMPtr<nsIObserverService> os = services::GetObserverService();
    if (NS_WARN_IF(!os)) {
      return nullptr;
    }

    RefPtr<LaunchObserver> ref = new LaunchObserver();

    nsresult rv =
        os->AddObserver(ref, "BrowserChrome:Ready", /* weakRef */ false);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return nullptr;
    }

    return ref.forget();
  }

  void Cancel() {
    nsCOMPtr<nsIObserverService> os = services::GetObserverService();
    if (os) {
      os->RemoveObserver(this, "BrowserChrome:Ready");
    }
    mPromise->Reject(NS_ERROR_ABORT, __func__);
  }

  GenericPromise* Promise() { return mPromise; }

  NS_DECL_ISUPPORTS
};

NS_IMPL_ISUPPORTS(LaunchObserver, nsIObserver);

#endif  // MOZ_WIDGET_ANDROID

}  // anonymous namespace

RefPtr<ClientOpPromise> ClientOpenWindowInCurrentProcess(
    const ClientOpenWindowArgs& aArgs) {
  RefPtr<ClientOpPromise::Private> promise =
      new ClientOpPromise::Private(__func__);

#ifdef MOZ_WIDGET_ANDROID
  // This isn't currently available on GeckoView because we have no way of
  // knowing which app to launch. Bug 1511033.
  if (!jni::IsFennec()) {
    promise->Reject(NS_ERROR_NOT_IMPLEMENTED, __func__);
    return promise.forget();
  }

  // This fires an intent that will start launching Fennec and foreground it,
  // if necessary.  We create an observer so that we can determine when
  // the launch has completed.
  RefPtr<LaunchObserver> launchObserver = LaunchObserver::Create();
  java::GeckoApp::LaunchOrBringToFront();
  // If we get the NOT_AVAILABLE error that means the browser is still
  // launching on android.  Use the observer we created above to wait
  // until the launch completes and then try to open the window again.
  if (rv == NS_ERROR_NOT_AVAILABLE && launchObserver) {
    RefPtr<GenericPromise> p = launchObserver->Promise();
    p->Then(
        SystemGroup::EventTargetFor(TaskCategory::Other), __func__,
        [aArgs, promise](bool aResult) {
          nsCOMPtr<nsPIDOMWindowOuter> outerWindow;
          nsresult rv = OpenWindow(aArgs, getter_AddRefs(outerWindow));
          if (NS_WARN_IF(NS_FAILED(rv))) {
            promise->Reject(rv, __func__);
            return;
          }

          WaitForLoad(aArgs, outerWindow, promise);
        },
        [promise](nsresult aResult) { promise->Reject(aResult, __func__); });
    return promise.forget();
  }

  // If we didn't get the NOT_AVAILABLE error then there is no need
  // wait for the browser to launch.  Cancel the observer so that it
  // will release.
  if (launchObserver) {
    launchObserver->Cancel();
  }
#endif  // MOZ_WIDGET_ANDROID

  nsCOMPtr<nsPIDOMWindowOuter> outerWindow;
  ErrorResult rv;
  OpenWindow(aArgs, getter_AddRefs(outerWindow), rv);

  if (NS_WARN_IF(rv.Failed())) {
    promise->Reject(rv, __func__);
    return promise.forget();
  }

  MOZ_DIAGNOSTIC_ASSERT(outerWindow);
  WaitForLoad(aArgs, outerWindow, promise);

  return promise.forget();
}

}  // namespace dom
}  // namespace mozilla
