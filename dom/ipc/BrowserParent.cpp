/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/basictypes.h"

#include "BrowserParent.h"

#ifdef ACCESSIBILITY
#  include "mozilla/a11y/DocAccessibleParent.h"
#  include "nsAccessibilityService.h"
#endif
#include "mozilla/BrowserElementParent.h"
#include "mozilla/dom/BlobImpl.h" //MY
#include "mozilla/dom/BrowsingContextGroup.h"
#include "mozilla/dom/CancelContentJSOptionsBinding.h"
#include "mozilla/dom/ChromeMessageSender.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/ContentProcessManager.h"
#include "mozilla/dom/DataTransfer.h"
#include "mozilla/dom/DataTransferItemList.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/FrameCrashedEvent.h"
#include "mozilla/dom/indexedDB/ActorsParent.h"
#include "mozilla/dom/IPCBlobUtils.h"
#include "mozilla/dom/BrowserBridgeParent.h"
#include "mozilla/dom/RemoteWebProgress.h"
#include "mozilla/dom/RemoteWebProgressRequest.h"
#include "mozilla/dom/SessionStoreUtils.h"
#include "mozilla/dom/SessionStoreUtilsBinding.h"
#include "mozilla/dom/UserActivation.h"
#include "mozilla/EventStateManager.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/DataSurfaceHelpers.h"
#include "mozilla/gfx/GPUProcessManager.h"
#include "mozilla/Hal.h"
#include "mozilla/IMEStateManager.h"
#include "mozilla/layers/AsyncDragMetrics.h"
#include "mozilla/layers/InputAPZContext.h"
#include "mozilla/layout/RemoteLayerTreeOwner.h"
#include "mozilla/plugins/PPluginWidgetParent.h"
#include "mozilla/LookAndFeel.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/net/NeckoChild.h"
#include "mozilla/Preferences.h"
#include "mozilla/PresShell.h"
#include "mozilla/ProcessHangMonitor.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/TextEvents.h"
#include "mozilla/TouchEvents.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/Unused.h"
#include "nsCOMPtr.h"
#include "nsContentAreaDragDrop.h"
#include "nsContentUtils.h"
#include "nsDebug.h"
#include "nsFocusManager.h"
#include "nsFrameLoader.h"
#include "nsFrameLoaderOwner.h"
#include "nsFrameManager.h"
#include "nsIAppWindow.h"
#include "nsIBaseWindow.h"
#include "nsIBrowser.h"
#include "nsIContent.h"
#include "nsIDocShell.h"
#include "nsIDocShellTreeOwner.h"
#include "nsImportModule.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsILoadInfo.h"
#include "nsIPromptFactory.h"
#include "nsIURI.h"
#include "nsIWebBrowserChrome.h"
#include "nsIWebProtocolHandlerRegistrar.h"
#include "nsIWindowWatcher.h"
#include "nsIXPConnect.h"
#include "nsIXULBrowserWindow.h"
#include "nsViewManager.h"
#include "nsVariant.h"
#include "nsIWidget.h"
#include "nsNetUtil.h"
#ifndef XP_WIN
#  include "nsJARProtocolHandler.h"
#endif
#include "nsPIDOMWindow.h"
#include "nsPrintfCString.h"
#include "nsQueryObject.h"
#include "nsServiceManagerUtils.h"
#include "nsThreadUtils.h"
#include "PermissionMessageUtils.h"
#include "StructuredCloneData.h"
#include "ColorPickerParent.h"
#include "FilePickerParent.h"
#include "BrowserChild.h"
#include "LoadContext.h"
#include "nsNetCID.h"
#include "nsIAuthInformation.h"
#include "nsIAuthPromptCallback.h"
#include "nsAuthInformationHolder.h"
#include "nsICancelable.h"
#include "gfxUtils.h"
#include "nsILoginManagerPrompter.h"
#include "nsPIWindowRoot.h"
#include "nsIAuthPrompt2.h"
#include "gfxDrawable.h"
#include "ImageOps.h"
#include "UnitTransforms.h"
#include <algorithm>
#include "mozilla/NullPrincipal.h"
#include "mozilla/WebBrowserPersistDocumentParent.h"
#include "ProcessPriorityManager.h"
#include "nsString.h"
#include "IHistory.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "MMPrinter.h"
#include "SessionStoreFunctions.h"

#ifdef XP_WIN
#  include "mozilla/plugins/PluginWidgetParent.h"
#endif

#if defined(XP_WIN) && defined(ACCESSIBILITY)
#  include "mozilla/a11y/AccessibleWrap.h"
#  include "mozilla/a11y/Compatibility.h"
#  include "mozilla/a11y/nsWinUtils.h"
#endif

#ifdef MOZ_ANDROID_HISTORY
#  include "GeckoViewHistory.h"
#endif

using namespace mozilla::dom;
using namespace mozilla::ipc;
using namespace mozilla::layers;
using namespace mozilla::layout;
using namespace mozilla::services;
using namespace mozilla::widget;
using namespace mozilla::gfx;

using mozilla::LazyLogModule;
using mozilla::StaticAutoPtr;
using mozilla::Unused;

LazyLogModule gBrowserFocusLog("BrowserFocus");

#define LOGBROWSERFOCUS(args) \
  MOZ_LOG(gBrowserFocusLog, mozilla::LogLevel::Debug, args)

/* static */
StaticAutoPtr<nsTArray<BrowserParent*>> BrowserParent::sFocusStack;

// The flags passed by the webProgress notifications are 16 bits shifted
// from the ones registered by webProgressListeners.
#define NOTIFY_FLAG_SHIFT 16

namespace mozilla {
namespace dom {

BrowserParent::LayerToBrowserParentTable*
    BrowserParent::sLayerToBrowserParentTable = nullptr;

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(BrowserParent)
  NS_INTERFACE_MAP_ENTRY(nsIRemoteTab)
  NS_INTERFACE_MAP_ENTRY(nsIAuthPromptProvider)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIRemoteTab)
NS_INTERFACE_MAP_END
NS_IMPL_CYCLE_COLLECTION(BrowserParent, mFrameElement, mBrowserDOMWindow,
                         mLoadContext, mFrameLoader, mBrowsingContext)
NS_IMPL_CYCLE_COLLECTING_ADDREF(BrowserParent)
NS_IMPL_CYCLE_COLLECTING_RELEASE(BrowserParent)

BrowserParent::BrowserParent(ContentParent* aManager, const TabId& aTabId,
                             const TabContext& aContext,
                             CanonicalBrowsingContext* aBrowsingContext,
                             uint32_t aChromeFlags,
                             BrowserBridgeParent* aBrowserBridgeParent)
    : TabContext(aContext),
      mTabId(aTabId),
      mManager(aManager),
      mBrowsingContext(aBrowsingContext),
      mLoadContext(nullptr),
      mFrameElement(nullptr),
      mBrowserDOMWindow(nullptr),
      mFrameLoader(nullptr),
      mChromeFlags(aChromeFlags),
      mBrowserBridgeParent(aBrowserBridgeParent),
      mContentCache(*this),
      mRemoteLayerTreeOwner{},
      mLayerTreeEpoch{1},
      mChildToParentConversionMatrix{},
      mRect(0, 0, 0, 0),
      mDimensions(0, 0),
      mOrientation(0),
      mDPI(0),
      mRounding(0),
      mDefaultScale(0),
      mUpdatedDimensions(false),
      mSizeMode(nsSizeMode_Normal),
      mClientOffset{},
      mChromeOffset{},
      mInitialDataTransferItems{},
      mDnDVisualization{},
      mDragValid(false),
      mDragRect{},
      mDragPrincipal{},
      mCreatingWindow(false),
      mDelayedURL{},
      mDelayedFrameScripts{},
      mCursor(eCursorInvalid),
      mCustomCursor{},
      mCustomCursorHotspotX(0),
      mCustomCursorHotspotY(0),
      mVerifyDropLinks{},
      mDocShellIsActive(false),
      mMarkedDestroying(false),
      mIsDestroyed(false),
      mTabSetsCursor(false),
      mHasContentOpener(false),
      mPreserveLayers(false),
      mRenderLayers(true),
      mActiveInPriorityManager(false),
      mHasLayers(false),
      mHasPresented(false),
      mHasBeforeUnload(false),
      mIsReadyToHandleInputEvents(false),
      mIsMouseEnterIntoWidgetEventSuppressed(false) {
  MOZ_ASSERT(aManager);
  // When the input event queue is disabled, we don't need to handle the case
  // that some input events are dispatched before PBrowserConstructor.
  mIsReadyToHandleInputEvents = !ContentParent::IsInputEventQueueSupported();
}

BrowserParent::~BrowserParent() {}

/* static */
void BrowserParent::InitializeStatics() {
  MOZ_ASSERT(XRE_IsParentProcess());
  sFocusStack = new nsTArray<BrowserParent*>();
  ClearOnShutdown(&sFocusStack);
}

/* static */
BrowserParent* BrowserParent::GetFocused() {
  if (!sFocusStack) {
    return nullptr;
  }
  if (sFocusStack->IsEmpty()) {
    return nullptr;
  }
  return sFocusStack->LastElement();
}

/*static*/
BrowserParent* BrowserParent::GetFrom(nsFrameLoader* aFrameLoader) {
  if (!aFrameLoader) {
    return nullptr;
  }
  PBrowserParent* remoteBrowser = aFrameLoader->GetRemoteBrowser();
  return static_cast<BrowserParent*>(remoteBrowser);
}

/*static*/
BrowserParent* BrowserParent::GetFrom(nsIRemoteTab* aBrowserParent) {
  return static_cast<BrowserParent*>(aBrowserParent);
}

/*static*/
BrowserParent* BrowserParent::GetFrom(PBrowserParent* aBrowserParent) {
  return static_cast<BrowserParent*>(aBrowserParent);
}

/*static*/
BrowserParent* BrowserParent::GetFrom(nsIContent* aContent) {
  RefPtr<nsFrameLoaderOwner> loaderOwner = do_QueryObject(aContent);
  if (!loaderOwner) {
    return nullptr;
  }
  RefPtr<nsFrameLoader> frameLoader = loaderOwner->GetFrameLoader();
  return GetFrom(frameLoader);
}

/* static */
BrowserParent* BrowserParent::GetBrowserParentFromLayersId(
    layers::LayersId aLayersId) {
  if (!sLayerToBrowserParentTable) {
    return nullptr;
  }
  return sLayerToBrowserParentTable->Get(uint64_t(aLayersId));
}

/*static*/
TabId BrowserParent::GetTabIdFrom(nsIDocShell* docShell) {
  nsCOMPtr<nsIBrowserChild> browserChild(BrowserChild::GetFrom(docShell));
  if (browserChild) {
    return static_cast<BrowserChild*>(browserChild.get())->GetTabId();
  }
  return TabId(0);
}

void BrowserParent::AddBrowserParentToTable(layers::LayersId aLayersId,
                                            BrowserParent* aBrowserParent) {
  if (!sLayerToBrowserParentTable) {
    sLayerToBrowserParentTable = new LayerToBrowserParentTable();
  }
  sLayerToBrowserParentTable->Put(uint64_t(aLayersId), aBrowserParent);
}

void BrowserParent::RemoveBrowserParentFromTable(layers::LayersId aLayersId) {
  if (!sLayerToBrowserParentTable) {
    return;
  }
  sLayerToBrowserParentTable->Remove(uint64_t(aLayersId));
  if (sLayerToBrowserParentTable->Count() == 0) {
    delete sLayerToBrowserParentTable;
    sLayerToBrowserParentTable = nullptr;
  }
}

already_AddRefed<nsILoadContext> BrowserParent::GetLoadContext() {
  nsCOMPtr<nsILoadContext> loadContext;
  if (mLoadContext) {
    loadContext = mLoadContext;
  } else {
    bool isPrivate = mChromeFlags & nsIWebBrowserChrome::CHROME_PRIVATE_WINDOW;
    SetPrivateBrowsingAttributes(isPrivate);
    bool useTrackingProtection = false;
    nsCOMPtr<nsIDocShell> docShell = mFrameElement->OwnerDoc()->GetDocShell();
    if (docShell) {
      docShell->GetUseTrackingProtection(&useTrackingProtection);
    }
    loadContext = new LoadContext(
        GetOwnerElement(), true /* aIsContent */, isPrivate,
        mChromeFlags & nsIWebBrowserChrome::CHROME_REMOTE_WINDOW,
        useTrackingProtection, OriginAttributesRef());
    mLoadContext = loadContext;
  }
  return loadContext.forget();
}

/**
 * Will return nullptr if there is no outer window available for the
 * document hosting the owner element of this BrowserParent. Also will return
 * nullptr if that outer window is in the process of closing.
 */
already_AddRefed<nsPIDOMWindowOuter> BrowserParent::GetParentWindowOuter() {
  nsCOMPtr<nsIContent> frame = GetOwnerElement();
  if (!frame) {
    return nullptr;
  }

  nsCOMPtr<nsPIDOMWindowOuter> parent = frame->OwnerDoc()->GetWindow();
  if (!parent || parent->Closed()) {
    return nullptr;
  }

  return parent.forget();
}

already_AddRefed<nsIWidget> BrowserParent::GetTopLevelWidget() {
  if (RefPtr<Element> element = mFrameElement) {
    if (PresShell* presShell = element->OwnerDoc()->GetPresShell()) {
      nsViewManager* vm = presShell->GetViewManager();
      nsCOMPtr<nsIWidget> widget;
      vm->GetRootWidget(getter_AddRefs(widget));
      return widget.forget();
    }
  }
  return nullptr;
}

already_AddRefed<nsIWidget> BrowserParent::GetTextInputHandlingWidget() const {
  if (!mFrameElement) {
    return nullptr;
  }
  PresShell* presShell = mFrameElement->OwnerDoc()->GetPresShell();
  if (!presShell) {
    return nullptr;
  }
  nsPresContext* presContext = presShell->GetPresContext();
  if (!presContext) {
    return nullptr;
  }
  nsCOMPtr<nsIWidget> widget = presContext->GetTextInputHandlingWidget();
  return widget.forget();
}

already_AddRefed<nsIWidget> BrowserParent::GetWidget() const {
  if (!mFrameElement) {
    return nullptr;
  }
  nsCOMPtr<nsIWidget> widget = nsContentUtils::WidgetForContent(mFrameElement);
  if (!widget) {
    widget = nsContentUtils::WidgetForDocument(mFrameElement->OwnerDoc());
  }
  return widget.forget();
}

already_AddRefed<nsIWidget> BrowserParent::GetDocWidget() const {
  if (!mFrameElement) {
    return nullptr;
  }
  return do_AddRef(
      nsContentUtils::WidgetForDocument(mFrameElement->OwnerDoc()));
}

nsIXULBrowserWindow* BrowserParent::GetXULBrowserWindow() {
  if (!mFrameElement) {
    return nullptr;
  }

  nsCOMPtr<nsIDocShell> docShell = mFrameElement->OwnerDoc()->GetDocShell();
  if (!docShell) {
    return nullptr;
  }

  nsCOMPtr<nsIDocShellTreeOwner> treeOwner;
  docShell->GetTreeOwner(getter_AddRefs(treeOwner));
  if (!treeOwner) {
    return nullptr;
  }

  nsCOMPtr<nsIAppWindow> window = do_GetInterface(treeOwner);
  if (!window) {
    return nullptr;
  }

  nsCOMPtr<nsIXULBrowserWindow> xulBrowserWindow;
  window->GetXULBrowserWindow(getter_AddRefs(xulBrowserWindow));
  return xulBrowserWindow;
}

a11y::DocAccessibleParent* BrowserParent::GetTopLevelDocAccessible() const {
#ifdef ACCESSIBILITY
  // XXX Consider managing non top level PDocAccessibles with their parent
  // document accessible.
  const ManagedContainer<PDocAccessibleParent>& docs =
      ManagedPDocAccessibleParent();
  for (auto iter = docs.ConstIter(); !iter.Done(); iter.Next()) {
    auto doc = static_cast<a11y::DocAccessibleParent*>(iter.Get()->GetKey());
    if (doc->IsTopLevel()) {
      return doc;
    }
  }

  MOZ_ASSERT(docs.Count() == 0,
             "If there isn't a top level accessible doc "
             "there shouldn't be an accessible doc at all!");
#endif
  return nullptr;
}

RemoteLayerTreeOwner* BrowserParent::GetRenderFrame() {
  if (!mRemoteLayerTreeOwner.IsInitialized()) {
    return nullptr;
  }
  return &mRemoteLayerTreeOwner;
}

ParentShowInfo BrowserParent::GetShowInfo() {
  TryCacheDPIAndScale();
  if (mFrameElement) {
    nsAutoString name;
    mFrameElement->GetAttr(kNameSpaceID_None, nsGkAtoms::name, name);
    // FIXME(emilio, bug 1606660): allowfullscreen should probably move to
    // OwnerShowInfo.
    bool allowFullscreen =
        mFrameElement->HasAttr(kNameSpaceID_None, nsGkAtoms::allowfullscreen) ||
        mFrameElement->HasAttr(kNameSpaceID_None,
                               nsGkAtoms::mozallowfullscreen);
    bool isPrivate = mFrameElement->HasAttr(kNameSpaceID_None,
                                            nsGkAtoms::mozprivatebrowsing);
    bool isTransparent =
        nsContentUtils::IsChromeDoc(mFrameElement->OwnerDoc()) &&
        mFrameElement->HasAttr(kNameSpaceID_None, nsGkAtoms::transparent);
    return ParentShowInfo(name, allowFullscreen, isPrivate, false,
                          isTransparent, mDPI, mRounding, mDefaultScale.scale);
  }

  return ParentShowInfo(EmptyString(), false, false, false, false, mDPI,
                        mRounding, mDefaultScale.scale);
}

void BrowserParent::SetOwnerElement(Element* aElement) {
  // If we held previous content then unregister for its events.
  RemoveWindowListeners();

  // If we change top-level documents then we need to change our
  // registration with them.
  RefPtr<nsPIWindowRoot> curTopLevelWin, newTopLevelWin;
  if (mFrameElement) {
    curTopLevelWin = nsContentUtils::GetWindowRoot(mFrameElement->OwnerDoc());
  }
  if (aElement) {
    newTopLevelWin = nsContentUtils::GetWindowRoot(aElement->OwnerDoc());
  }
  bool isSameTopLevelWin = curTopLevelWin == newTopLevelWin;
  if (curTopLevelWin && !isSameTopLevelWin) {
    curTopLevelWin->RemoveBrowser(this);
  }

  // Update to the new content, and register to listen for events from it.
  mFrameElement = aElement;

  if (newTopLevelWin && !isSameTopLevelWin) {
    newTopLevelWin->AddBrowser(this);
  }

  if (mFrameElement) {
    bool useGlobalHistory = !mFrameElement->HasAttr(
        kNameSpaceID_None, nsGkAtoms::disableglobalhistory);
    Unused << SendSetUseGlobalHistory(useGlobalHistory);
  }

#if defined(XP_WIN) && defined(ACCESSIBILITY)
  if (!mIsDestroyed) {
    uintptr_t newWindowHandle = 0;
    if (nsCOMPtr<nsIWidget> widget = GetWidget()) {
      newWindowHandle =
          reinterpret_cast<uintptr_t>(widget->GetNativeData(NS_NATIVE_WINDOW));
    }
    Unused << SendUpdateNativeWindowHandle(newWindowHandle);
    a11y::DocAccessibleParent* doc = GetTopLevelDocAccessible();
    if (doc) {
      HWND hWnd = reinterpret_cast<HWND>(doc->GetEmulatedWindowHandle());
      if (hWnd) {
        HWND parentHwnd = reinterpret_cast<HWND>(newWindowHandle);
        if (parentHwnd != ::GetParent(hWnd)) {
          ::SetParent(hWnd, parentHwnd);
        }
      }
    }
  }
#endif

  AddWindowListeners();
  TryCacheDPIAndScale();

  // Try to send down WidgetNativeData, now that this BrowserParent is
  // associated with a widget.
  nsCOMPtr<nsIWidget> widget = GetTopLevelWidget();
  if (widget) {
    WindowsHandle widgetNativeData = reinterpret_cast<WindowsHandle>(
        widget->GetNativeData(NS_NATIVE_SHAREABLE_WINDOW));
    if (widgetNativeData) {
      Unused << SendSetWidgetNativeData(widgetNativeData);
    }
  }

  if (mRemoteLayerTreeOwner.IsInitialized()) {
    mRemoteLayerTreeOwner.OwnerContentChanged();
  }

  // Set our BrowsingContext's embedder if we're not embedded within a
  // BrowserBridgeParent.
  if (!GetBrowserBridgeParent() && mBrowsingContext && mFrameElement) {
    mBrowsingContext->SetEmbedderElement(mFrameElement);
  }

  VisitChildren([aElement](BrowserBridgeParent* aBrowser) {
    aBrowser->GetBrowserParent()->SetOwnerElement(aElement);
  });
}

NS_IMETHODIMP BrowserParent::GetOwnerElement(Element** aElement) {
  *aElement = do_AddRef(GetOwnerElement()).take();
  return NS_OK;
}

void BrowserParent::CacheFrameLoader(nsFrameLoader* aFrameLoader) {
  mFrameLoader = aFrameLoader;
}

void BrowserParent::AddWindowListeners() {
  if (mFrameElement) {
    if (nsCOMPtr<nsPIDOMWindowOuter> window =
            mFrameElement->OwnerDoc()->GetWindow()) {
      nsCOMPtr<EventTarget> eventTarget = window->GetTopWindowRoot();
      if (eventTarget) {
        eventTarget->AddEventListener(u"MozUpdateWindowPos"_ns, this, false,
                                      false);
        eventTarget->AddEventListener(u"fullscreenchange"_ns, this, false,
                                      false);
      }
    }
  }
}

void BrowserParent::RemoveWindowListeners() {
  if (mFrameElement && mFrameElement->OwnerDoc()->GetWindow()) {
    nsCOMPtr<nsPIDOMWindowOuter> window =
        mFrameElement->OwnerDoc()->GetWindow();
    nsCOMPtr<EventTarget> eventTarget = window->GetTopWindowRoot();
    if (eventTarget) {
      eventTarget->RemoveEventListener(u"MozUpdateWindowPos"_ns, this, false);
      eventTarget->RemoveEventListener(u"fullscreenchange"_ns, this, false);
    }
  }
}

void BrowserParent::DestroyInternal() {
  PopFocus(this);

  RemoveWindowListeners();

#ifdef ACCESSIBILITY
  if (a11y::DocAccessibleParent* tabDoc = GetTopLevelDocAccessible()) {
    tabDoc->Destroy();
  }
#endif

  // If this fails, it's most likely due to a content-process crash,
  // and auto-cleanup will kick in.  Otherwise, the child side will
  // destroy itself and send back __delete__().
  Unused << SendDestroy();

#ifdef XP_WIN
  // Let all PluginWidgets know we are tearing down. Prevents
  // these objects from sending async events after the child side
  // is shut down.
  const ManagedContainer<PPluginWidgetParent>& kids =
      ManagedPPluginWidgetParent();
  for (auto iter = kids.ConstIter(); !iter.Done(); iter.Next()) {
    static_cast<mozilla::plugins::PluginWidgetParent*>(iter.Get()->GetKey())
        ->ParentDestroy();
  }
#endif
}

void BrowserParent::Destroy() {
  // Aggressively release the window to avoid leaking the world in shutdown
  // corner cases.
  mBrowserDOMWindow = nullptr;

  if (mIsDestroyed) {
    return;
  }

  DestroyInternal();

  mIsDestroyed = true;

  Manager()->NotifyTabDestroying();

  mMarkedDestroying = true;
}

mozilla::ipc::IPCResult BrowserParent::RecvEnsureLayersConnected(
    CompositorOptions* aCompositorOptions) {
  if (mRemoteLayerTreeOwner.IsInitialized()) {
    mRemoteLayerTreeOwner.EnsureLayersConnected(aCompositorOptions);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::Recv__delete__() {
  MOZ_RELEASE_ASSERT(XRE_IsParentProcess());
  Manager()->NotifyTabDestroyed(mTabId, mMarkedDestroying);
  return IPC_OK();
}

void BrowserParent::ActorDestroy(ActorDestroyReason why) {
  ContentProcessManager::GetSingleton()->UnregisterRemoteFrame(mTabId);

  if (mRemoteLayerTreeOwner.IsInitialized()) {
    // It's important to unmap layers after the remote browser has been
    // destroyed, otherwise it may still send messages to the compositor which
    // will reject them, causing assertions.
    RemoveBrowserParentFromTable(mRemoteLayerTreeOwner.GetLayersId());
    mRemoteLayerTreeOwner.Destroy();
  }

  // Even though BrowserParent::Destroy calls this, we need to do it here too in
  // case of a crash.
  BrowserParent::PopFocus(this);

  // Prevent executing ContentParent::NotifyTabDestroying in
  // BrowserParent::Destroy() called by frameLoader->DestroyComplete() below
  // when tab crashes in contentprocess because ContentParent::ActorDestroy()
  // in main process will be triggered before this function
  // and remove the process information that
  // ContentParent::NotifyTabDestroying need from mContentParentMap.

  // When tab crashes in content process,
  // there is no need to call ContentParent::NotifyTabDestroying
  // because the jobs in ContentParent::NotifyTabDestroying
  // will be done by ContentParent::ActorDestroy.
  if (XRE_IsContentProcess() && why == AbnormalShutdown && !mIsDestroyed) {
    DestroyInternal();
    mIsDestroyed = true;
  }

  // Tell our embedder that the tab is now going away unless we're an
  // out-of-process iframe.
  RefPtr<nsFrameLoader> frameLoader = GetFrameLoader(true);
  nsCOMPtr<nsIObserverService> os = services::GetObserverService();
  if (frameLoader) {
    nsCOMPtr<Element> frameElement(mFrameElement);
    ReceiveMessage(CHILD_PROCESS_SHUTDOWN_MESSAGE, false, nullptr);

    if (!mBrowsingContext->GetParent()) {
      // If this is a top-level BrowsingContext, tell the frameloader it's time
      // to go away. Otherwise, this is a subframe crash, and we can keep the
      // frameloader around.
      frameLoader->DestroyComplete();
    }

    if (why == AbnormalShutdown && os) {
      os->NotifyObservers(ToSupports(frameLoader), "oop-frameloader-crashed",
                          nullptr);
      RefPtr<nsFrameLoaderOwner> owner = do_QueryObject(frameElement);
      if (owner) {
        RefPtr<nsFrameLoader> currentFrameLoader = owner->GetFrameLoader();
        // It's possible that the frameloader owner has already moved on
        // and created a new frameloader. If so, we don't fire the event,
        // since the frameloader owner has clearly moved on.
        if (currentFrameLoader == frameLoader) {
          nsString eventName;
          MessageChannel* channel = GetIPCChannel();
          if (channel && !channel->DoBuildIDsMatch()) {
            eventName = NS_LITERAL_STRING("oop-browser-buildid-mismatch");
          } else {
            eventName = NS_LITERAL_STRING("oop-browser-crashed");
          }

          dom::FrameCrashedEventInit init;
          init.mBubbles = true;
          init.mCancelable = true;
          init.mBrowsingContextId = mBrowsingContext->Id();
          init.mIsTopFrame = !mBrowsingContext->GetParent();

          RefPtr<dom::FrameCrashedEvent> event =
              dom::FrameCrashedEvent::Constructor(frameElement->OwnerDoc(),
                                                  eventName, init);
          event->SetTrusted(true);
          EventDispatcher::DispatchDOMEvent(frameElement, nullptr, event,
                                            nullptr, nullptr);
        }
      }
    }
  }

  mFrameLoader = nullptr;

  if (os) {
    os->NotifyObservers(NS_ISUPPORTS_CAST(nsIRemoteTab*, this),
                        "ipc:browser-destroyed", nullptr);
  }
}

mozilla::ipc::IPCResult BrowserParent::RecvMoveFocus(
    const bool& aForward, const bool& aForDocumentNavigation) {
  LOGBROWSERFOCUS(("RecvMoveFocus %p, aForward: %d, aForDocumentNavigation: %d",
                   this, aForward, aForDocumentNavigation));
  BrowserBridgeParent* bridgeParent = GetBrowserBridgeParent();
  if (bridgeParent) {
    mozilla::Unused << bridgeParent->SendMoveFocus(aForward,
                                                   aForDocumentNavigation);
    return IPC_OK();
  }

  RefPtr<nsFocusManager> fm = nsFocusManager::GetFocusManager();
  if (fm) {
    RefPtr<Element> dummy;

    uint32_t type =
        aForward
            ? (aForDocumentNavigation
                   ? static_cast<uint32_t>(
                         nsIFocusManager::MOVEFOCUS_FORWARDDOC)
                   : static_cast<uint32_t>(nsIFocusManager::MOVEFOCUS_FORWARD))
            : (aForDocumentNavigation
                   ? static_cast<uint32_t>(
                         nsIFocusManager::MOVEFOCUS_BACKWARDDOC)
                   : static_cast<uint32_t>(
                         nsIFocusManager::MOVEFOCUS_BACKWARD));
    fm->MoveFocus(nullptr, mFrameElement, type, nsIFocusManager::FLAG_BYKEY,
                  getter_AddRefs(dummy));
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvSizeShellTo(
    const uint32_t& aFlags, const int32_t& aWidth, const int32_t& aHeight,
    const int32_t& aShellItemWidth, const int32_t& aShellItemHeight) {
  NS_ENSURE_TRUE(mFrameElement, IPC_OK());

  nsCOMPtr<nsIDocShell> docShell = mFrameElement->OwnerDoc()->GetDocShell();
  NS_ENSURE_TRUE(docShell, IPC_OK());

  nsCOMPtr<nsIDocShellTreeOwner> treeOwner;
  nsresult rv = docShell->GetTreeOwner(getter_AddRefs(treeOwner));
  NS_ENSURE_SUCCESS(rv, IPC_OK());

  int32_t width = aWidth;
  int32_t height = aHeight;

  if (aFlags & nsIEmbeddingSiteWindow::DIM_FLAGS_IGNORE_CX) {
    width = mDimensions.width;
  }

  if (aFlags & nsIEmbeddingSiteWindow::DIM_FLAGS_IGNORE_CY) {
    height = mDimensions.height;
  }

  nsCOMPtr<nsIAppWindow> appWin(do_GetInterface(treeOwner));
  NS_ENSURE_TRUE(appWin, IPC_OK());
  appWin->SizeShellToWithLimit(width, height, aShellItemWidth,
                               aShellItemHeight);

  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvDropLinks(
    nsTArray<nsString>&& aLinks) {
  nsCOMPtr<nsIBrowser> browser =
      mFrameElement ? mFrameElement->AsBrowser() : nullptr;
  if (browser) {
    // Verify that links have not been modified by the child. If links have
    // not been modified then it's safe to load those links using the
    // SystemPrincipal. If they have been modified by web content, then
    // we use a NullPrincipal which still allows to load web links.
    bool loadUsingSystemPrincipal = true;
    if (aLinks.Length() != mVerifyDropLinks.Length()) {
      loadUsingSystemPrincipal = false;
    }
    for (uint32_t i = 0; i < aLinks.Length(); i++) {
      if (loadUsingSystemPrincipal) {
        if (!aLinks[i].Equals(mVerifyDropLinks[i])) {
          loadUsingSystemPrincipal = false;
        }
      }
    }
    mVerifyDropLinks.Clear();
    nsCOMPtr<nsIPrincipal> triggeringPrincipal;
    if (loadUsingSystemPrincipal) {
      triggeringPrincipal = nsContentUtils::GetSystemPrincipal();
    } else {
      triggeringPrincipal = NullPrincipal::CreateWithoutOriginAttributes();
    }
    browser->DropLinks(aLinks, triggeringPrincipal);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvEvent(const RemoteDOMEvent& aEvent) {
  RefPtr<Event> event = aEvent.mEvent;
  NS_ENSURE_TRUE(event, IPC_OK());

  RefPtr<EventTarget> target = mFrameElement;
  NS_ENSURE_TRUE(target, IPC_OK());

  event->SetOwner(target);

  target->DispatchEvent(*event);
  return IPC_OK();
}

bool BrowserParent::SendLoadRemoteScript(const nsString& aURL,
                                         const bool& aRunInGlobalScope) {
  if (mCreatingWindow) {
    mDelayedFrameScripts.AppendElement(
        FrameScriptInfo(aURL, aRunInGlobalScope));
    return true;
  }

  MOZ_ASSERT(mDelayedFrameScripts.IsEmpty());
  return PBrowserParent::SendLoadRemoteScript(aURL, aRunInGlobalScope);
}

void BrowserParent::LoadURL(nsIURI* aURI) {
  MOZ_ASSERT(aURI);

  if (mIsDestroyed) {
    return;
  }

  nsCString spec;
  aURI->GetSpec(spec);

  if (mCreatingWindow) {
    // Don't send the message if the child wants to load its own URL.
    MOZ_ASSERT(mDelayedURL.IsEmpty());
    mDelayedURL = spec;
    return;
  }

  Unused << SendLoadURL(spec, GetShowInfo());
}

void BrowserParent::ResumeLoad(uint64_t aPendingSwitchID) {
  MOZ_ASSERT(aPendingSwitchID != 0);

  if (NS_WARN_IF(mIsDestroyed)) {
    return;
  }

  Unused << SendResumeLoad(aPendingSwitchID, GetShowInfo());
}

void BrowserParent::InitRendering() {
  MOZ_ASSERT(!mRemoteLayerTreeOwner.IsInitialized());
  mRemoteLayerTreeOwner.Initialize(this);
  MOZ_ASSERT(mRemoteLayerTreeOwner.IsInitialized());

  layers::LayersId layersId = mRemoteLayerTreeOwner.GetLayersId();
  AddBrowserParentToTable(layersId, this);

  TextureFactoryIdentifier textureFactoryIdentifier;
  mRemoteLayerTreeOwner.GetTextureFactoryIdentifier(&textureFactoryIdentifier);
  Unused << SendInitRendering(textureFactoryIdentifier, layersId,
                              mRemoteLayerTreeOwner.GetCompositorOptions(),
                              mRemoteLayerTreeOwner.IsLayersConnected());
}

void BrowserParent::MaybeShowFrame() {
  RefPtr<nsFrameLoader> frameLoader = GetFrameLoader();
  if (!frameLoader) {
    return;
  }
  frameLoader->MaybeShowFrame();
}

void BrowserParent::Show(const ScreenIntSize& size, bool aParentIsActive) {
  mDimensions = size;
  if (mIsDestroyed) {
    return;
  }

  MOZ_ASSERT(mRemoteLayerTreeOwner.IsInitialized());

  nsCOMPtr<nsISupports> container = mFrameElement->OwnerDoc()->GetContainer();
  nsCOMPtr<nsIBaseWindow> baseWindow = do_QueryInterface(container);
  nsCOMPtr<nsIWidget> mainWidget;
  baseWindow->GetMainWidget(getter_AddRefs(mainWidget));
  mSizeMode = mainWidget ? mainWidget->SizeMode() : nsSizeMode_Normal;

  OwnerShowInfo ownerInfo(size, aParentIsActive, mSizeMode);
  Unused << SendShow(GetShowInfo(), ownerInfo);
}

mozilla::ipc::IPCResult BrowserParent::RecvSetDimensions(
    const uint32_t& aFlags, const int32_t& aX, const int32_t& aY,
    const int32_t& aCx, const int32_t& aCy, const double& aScale) {
  MOZ_ASSERT(!(aFlags & nsIEmbeddingSiteWindow::DIM_FLAGS_SIZE_INNER),
             "We should never see DIM_FLAGS_SIZE_INNER here!");

  NS_ENSURE_TRUE(mFrameElement, IPC_OK());
  nsCOMPtr<nsIDocShell> docShell = mFrameElement->OwnerDoc()->GetDocShell();
  NS_ENSURE_TRUE(docShell, IPC_OK());
  nsCOMPtr<nsIDocShellTreeOwner> treeOwner;
  docShell->GetTreeOwner(getter_AddRefs(treeOwner));
  nsCOMPtr<nsIBaseWindow> treeOwnerAsWin = do_QueryInterface(treeOwner);
  NS_ENSURE_TRUE(treeOwnerAsWin, IPC_OK());

  // We only care about the parameters that actually changed, see more details
  // in `BrowserChild::SetDimensions()`.
  // Note that `BrowserChild::SetDimensions()` may be called before receiving
  // our `SendUIResolutionChanged()` call.  Therefore, if given each cordinate
  // shouldn't be ignored, we need to recompute it if DPI has been changed.
  // And also note that don't use `mDefaultScale.scale` here since it may be
  // different from the result of `GetUnscaledDevicePixelsPerCSSPixel()`.
  double currentScale;
  treeOwnerAsWin->GetUnscaledDevicePixelsPerCSSPixel(&currentScale);

  int32_t x = aX;
  int32_t y = aY;
  if (aFlags & nsIEmbeddingSiteWindow::DIM_FLAGS_POSITION) {
    if (aFlags & nsIEmbeddingSiteWindow::DIM_FLAGS_IGNORE_X) {
      int32_t unused;
      treeOwnerAsWin->GetPosition(&x, &unused);
    } else if (aScale != currentScale) {
      x = x * currentScale / aScale;
    }

    if (aFlags & nsIEmbeddingSiteWindow::DIM_FLAGS_IGNORE_Y) {
      int32_t unused;
      treeOwnerAsWin->GetPosition(&unused, &y);
    } else if (aScale != currentScale) {
      y = y * currentScale / aScale;
    }
  }

  int32_t cx = aCx;
  int32_t cy = aCy;
  if (aFlags & nsIEmbeddingSiteWindow::DIM_FLAGS_SIZE_OUTER) {
    if (aFlags & nsIEmbeddingSiteWindow::DIM_FLAGS_IGNORE_CX) {
      int32_t unused;
      treeOwnerAsWin->GetSize(&cx, &unused);
    } else if (aScale != currentScale) {
      cx = cx * currentScale / aScale;
    }

    if (aFlags & nsIEmbeddingSiteWindow::DIM_FLAGS_IGNORE_CY) {
      int32_t unused;
      treeOwnerAsWin->GetSize(&unused, &cy);
    } else if (aScale != currentScale) {
      cy = cy * currentScale / aScale;
    }
  }

  if (aFlags & nsIEmbeddingSiteWindow::DIM_FLAGS_POSITION &&
      aFlags & nsIEmbeddingSiteWindow::DIM_FLAGS_SIZE_OUTER) {
    treeOwnerAsWin->SetPositionAndSize(x, y, cx, cy, nsIBaseWindow::eRepaint);
    return IPC_OK();
  }

  if (aFlags & nsIEmbeddingSiteWindow::DIM_FLAGS_POSITION) {
    treeOwnerAsWin->SetPosition(x, y);
    mUpdatedDimensions = false;
    UpdatePosition();
    return IPC_OK();
  }

  if (aFlags & nsIEmbeddingSiteWindow::DIM_FLAGS_SIZE_OUTER) {
    treeOwnerAsWin->SetSize(cx, cy, true);
    return IPC_OK();
  }

  MOZ_ASSERT(false, "Unknown flags!");
  return IPC_FAIL_NO_REASON(this);
}

nsresult BrowserParent::UpdatePosition() {
  RefPtr<nsFrameLoader> frameLoader = GetFrameLoader();
  if (!frameLoader) {
    return NS_OK;
  }
  nsIntRect windowDims;
  NS_ENSURE_SUCCESS(frameLoader->GetWindowDimensions(windowDims),
                    NS_ERROR_FAILURE);
  UpdateDimensions(windowDims, mDimensions);
  return NS_OK;
}

void BrowserParent::UpdateDimensions(const nsIntRect& rect,
                                     const ScreenIntSize& size) {
  if (mIsDestroyed) {
    return;
  }
  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (!widget) {
    NS_WARNING("No widget found in BrowserParent::UpdateDimensions");
    return;
  }

  hal::ScreenConfiguration config;
  hal::GetCurrentScreenConfiguration(&config);
  hal::ScreenOrientation orientation = config.orientation();
  LayoutDeviceIntPoint clientOffset = GetClientOffset();
  LayoutDeviceIntPoint chromeOffset = -GetChildProcessOffset();

  if (!mUpdatedDimensions || mOrientation != orientation ||
      mDimensions != size || !mRect.IsEqualEdges(rect) ||
      clientOffset != mClientOffset || chromeOffset != mChromeOffset) {
    mUpdatedDimensions = true;
    mRect = rect;
    mDimensions = size;
    mOrientation = orientation;
    mClientOffset = clientOffset;
    mChromeOffset = chromeOffset;

    Unused << SendUpdateDimensions(GetDimensionInfo());
  }
}

DimensionInfo BrowserParent::GetDimensionInfo() {
  nsCOMPtr<nsIWidget> widget = GetWidget();
  MOZ_ASSERT(widget);
  CSSToLayoutDeviceScale widgetScale = widget->GetDefaultScale();

  LayoutDeviceIntRect devicePixelRect = ViewAs<LayoutDevicePixel>(
      mRect, PixelCastJustification::LayoutDeviceIsScreenForTabDims);
  LayoutDeviceIntSize devicePixelSize = ViewAs<LayoutDevicePixel>(
      mDimensions, PixelCastJustification::LayoutDeviceIsScreenForTabDims);

  CSSRect unscaledRect = devicePixelRect / widgetScale;
  CSSSize unscaledSize = devicePixelSize / widgetScale;
  DimensionInfo di(unscaledRect, unscaledSize, mOrientation, mClientOffset,
                   mChromeOffset);
  return di;
}

void BrowserParent::SizeModeChanged(const nsSizeMode& aSizeMode) {
  if (!mIsDestroyed && aSizeMode != mSizeMode) {
    mSizeMode = aSizeMode;
    Unused << SendSizeModeChanged(aSizeMode);
  }
}

void BrowserParent::UIResolutionChanged() {
  if (!mIsDestroyed) {
    // TryCacheDPIAndScale()'s cache is keyed off of
    // mDPI being greater than 0, so this invalidates it.
    mDPI = -1;
    TryCacheDPIAndScale();
    // If mDPI was set to -1 to invalidate it and then TryCacheDPIAndScale
    // fails to cache the values, then mDefaultScale.scale might be invalid.
    // We don't want to send that value to content. Just send -1 for it too in
    // that case.
    Unused << SendUIResolutionChanged(mDPI, mRounding,
                                      mDPI < 0 ? -1.0 : mDefaultScale.scale);
  }
}

void BrowserParent::ThemeChanged() {
  if (!mIsDestroyed) {
    // The theme has changed, and any cached values we had sent down
    // to the child have been invalidated. When this method is called,
    // LookAndFeel should have the up-to-date values, which we now
    // send down to the child. We do this for every remote tab for now,
    // but bug 1156934 has been filed to do it once per content process.
    Unused << SendThemeChanged(LookAndFeel::GetIntCache());
  }
}

void BrowserParent::HandleAccessKey(const WidgetKeyboardEvent& aEvent,
                                    nsTArray<uint32_t>& aCharCodes) {
  if (!mIsDestroyed) {
    // Note that we don't need to mark aEvent is posted to a remote process
    // because the event may be dispatched to it as normal keyboard event.
    // Therefore, we should use local copy to send it.
    WidgetKeyboardEvent localEvent(aEvent);
    Unused << SendHandleAccessKey(localEvent, aCharCodes);
  }
}

void BrowserParent::Activate() {
  LOGBROWSERFOCUS(("Activate %p", this));
  if (!mIsDestroyed) {
    PushFocus(this);  // Intentionally inside "if"
    Unused << Manager()->SendActivate(this);
  }
}

void BrowserParent::Deactivate(bool aWindowLowering) {
  LOGBROWSERFOCUS(("Deactivate %p", this));
  if (!aWindowLowering) {
    PopFocus(this);  // Intentionally outside the next "if"
  }
  if (!mIsDestroyed) {
    Unused << Manager()->SendDeactivate(this);
  }
}

#ifdef ACCESSIBILITY
a11y::PDocAccessibleParent* BrowserParent::AllocPDocAccessibleParent(
    PDocAccessibleParent* aParent, const uint64_t&, const uint32_t&,
    const IAccessibleHolder&) {
  return new a11y::DocAccessibleParent();
}

bool BrowserParent::DeallocPDocAccessibleParent(PDocAccessibleParent* aParent) {
  delete static_cast<a11y::DocAccessibleParent*>(aParent);
  return true;
}

mozilla::ipc::IPCResult BrowserParent::RecvPDocAccessibleConstructor(
    PDocAccessibleParent* aDoc, PDocAccessibleParent* aParentDoc,
    const uint64_t& aParentID, const uint32_t& aMsaaID,
    const IAccessibleHolder& aDocCOMProxy) {
  auto doc = static_cast<a11y::DocAccessibleParent*>(aDoc);

  // If this tab is already shutting down just mark the new actor as shutdown
  // and ignore it.  When the tab actor is destroyed it will be too.
  if (mIsDestroyed) {
    doc->MarkAsShutdown();
    return IPC_OK();
  }

  if (aParentDoc) {
    // A document should never directly be the parent of another document.
    // There should always be an outer doc accessible child of the outer
    // document containing the child.
    MOZ_ASSERT(aParentID);
    if (!aParentID) {
      return IPC_FAIL_NO_REASON(this);
    }

    auto parentDoc = static_cast<a11y::DocAccessibleParent*>(aParentDoc);
    mozilla::ipc::IPCResult added = parentDoc->AddChildDoc(doc, aParentID);
    if (!added) {
#  ifdef DEBUG
      return added;
#  else
      return IPC_OK();
#  endif
    }

#  ifdef XP_WIN
    MOZ_ASSERT(aDocCOMProxy.IsNull());
    a11y::WrapperFor(doc)->SetID(aMsaaID);
    if (a11y::nsWinUtils::IsWindowEmulationStarted()) {
      doc->SetEmulatedWindowHandle(parentDoc->GetEmulatedWindowHandle());
    }
#  endif

    return IPC_OK();
  } else {
    // null aParentDoc means this document is at the top level in the child
    // process.  That means it makes no sense to get an id for an accessible
    // that is its parent.
    MOZ_ASSERT(!aParentID);
    if (aParentID) {
      return IPC_FAIL_NO_REASON(this);
    }

    doc->SetTopLevel();
    a11y::DocManager::RemoteDocAdded(doc);
#  ifdef XP_WIN
    a11y::WrapperFor(doc)->SetID(aMsaaID);
    MOZ_ASSERT(!aDocCOMProxy.IsNull());

    RefPtr<IAccessible> proxy(aDocCOMProxy.Get());
    doc->SetCOMInterface(proxy);
    doc->MaybeInitWindowEmulation();
    if (a11y::Accessible* outerDoc = doc->OuterDocOfRemoteBrowser()) {
      doc->SendParentCOMProxy(outerDoc);
    }
#  endif
  }
  return IPC_OK();
}
#endif

PFilePickerParent* BrowserParent::AllocPFilePickerParent(const nsString& aTitle,
                                                         const int16_t& aMode) {
  return new FilePickerParent(aTitle, aMode);
}

bool BrowserParent::DeallocPFilePickerParent(PFilePickerParent* actor) {
  delete actor;
  return true;
}

IPCResult BrowserParent::RecvIndexedDBPermissionRequest(
    nsIPrincipal* aPrincipal, IndexedDBPermissionRequestResolver&& aResolve) {
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIPrincipal> principal(aPrincipal);
  if (!principal) {
    return IPC_FAIL_NO_REASON(this);
  }

  if (NS_WARN_IF(!mFrameElement)) {
    return IPC_FAIL_NO_REASON(this);
  }

  RefPtr<indexedDB::PermissionRequestHelper> actor =
      new indexedDB::PermissionRequestHelper(mFrameElement, principal,
                                             aResolve);

  indexedDB::PermissionRequestBase::PermissionValue permission;
  nsresult rv = actor->PromptIfNeeded(&permission);
  if (NS_FAILED(rv)) {
    return IPC_FAIL_NO_REASON(this);
  }

  if (permission != indexedDB::PermissionRequestBase::kPermissionPrompt) {
    aResolve(permission);
  }

  return IPC_OK();
}

IPCResult BrowserParent::RecvPWindowGlobalConstructor(
    PWindowGlobalParent* aActor, const WindowGlobalInit& aInit) {
  static_cast<WindowGlobalParent*>(aActor)->Init(aInit);
  return IPC_OK();
}

PWindowGlobalParent* BrowserParent::AllocPWindowGlobalParent(
    const WindowGlobalInit& aInit) {
  // Reference freed in DeallocPWindowGlobalParent.
  return do_AddRef(new WindowGlobalParent(aInit, /* inproc */ false)).take();
}

bool BrowserParent::DeallocPWindowGlobalParent(PWindowGlobalParent* aActor) {
  // Free reference from AllocPWindowGlobalParent.
  static_cast<WindowGlobalParent*>(aActor)->Release();
  return true;
}

IPCResult BrowserParent::RecvPBrowserBridgeConstructor(
    PBrowserBridgeParent* aActor, const nsString& aName,
    const nsString& aRemoteType, BrowsingContext* aBrowsingContext,
    const uint32_t& aChromeFlags) {
  static_cast<BrowserBridgeParent*>(aActor)->Init(
      aName, aRemoteType, CanonicalBrowsingContext::Cast(aBrowsingContext),
      aChromeFlags);
  return IPC_OK();
}

already_AddRefed<PBrowserBridgeParent> BrowserParent::AllocPBrowserBridgeParent(
    const nsString& aName, const nsString& aRemoteType,
    BrowsingContext* aBrowsingContext, const uint32_t& aChromeFlags) {
  return do_AddRef(new BrowserBridgeParent());
}

void BrowserParent::SendMouseEvent(const nsAString& aType, float aX, float aY,
                                   int32_t aButton, int32_t aClickCount,
                                   int32_t aModifiers) {
  if (!mIsDestroyed) {
    Unused << PBrowserParent::SendMouseEvent(nsString(aType), aX, aY, aButton,
                                             aClickCount, aModifiers);
  }
}

void BrowserParent::SendRealMouseEvent(WidgetMouseEvent& aEvent) {
  if (mIsDestroyed) {
    return;
  }
  aEvent.mRefPoint = TransformParentToChild(aEvent.mRefPoint);

  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (widget) {
    // When we mouseenter the tab, the tab's cursor should
    // become the current cursor.  When we mouseexit, we stop.
    if (eMouseEnterIntoWidget == aEvent.mMessage) {
      mTabSetsCursor = true;
      if (mCursor != eCursorInvalid) {
        widget->SetCursor(mCursor, mCustomCursor, mCustomCursorHotspotX,
                          mCustomCursorHotspotY);
      }
    } else if (eMouseExitFromWidget == aEvent.mMessage) {
      mTabSetsCursor = false;
    }
  }
  if (!mIsReadyToHandleInputEvents) {
    if (eMouseEnterIntoWidget == aEvent.mMessage) {
      mIsMouseEnterIntoWidgetEventSuppressed = true;
    } else if (eMouseExitFromWidget == aEvent.mMessage) {
      mIsMouseEnterIntoWidgetEventSuppressed = false;
    }
    return;
  }

  ScrollableLayerGuid guid;
  uint64_t blockId;
  ApzAwareEventRoutingToChild(&guid, &blockId, nullptr);

  bool isInputPriorityEventEnabled = Manager()->IsInputPriorityEventEnabled();

  if (mIsMouseEnterIntoWidgetEventSuppressed) {
    // In the case that the BrowserParent suppressed the eMouseEnterWidget event
    // due to its corresponding BrowserChild wasn't ready to handle it, we have
    // to resend it when the BrowserChild is ready.
    mIsMouseEnterIntoWidgetEventSuppressed = false;
    WidgetMouseEvent localEvent(aEvent);
    localEvent.mMessage = eMouseEnterIntoWidget;
    DebugOnly<bool> ret =
        isInputPriorityEventEnabled
            ? SendRealMouseButtonEvent(localEvent, guid, blockId)
            : SendNormalPriorityRealMouseButtonEvent(localEvent, guid, blockId);
    NS_WARNING_ASSERTION(
        ret, "SendRealMouseButtonEvent(eMouseEnterIntoWidget) failed");
    MOZ_ASSERT(!ret || localEvent.HasBeenPostedToRemoteProcess());
  }

  if (eMouseMove == aEvent.mMessage) {
    if (aEvent.mReason == WidgetMouseEvent::eSynthesized) {
      DebugOnly<bool> ret =
          isInputPriorityEventEnabled
              ? SendSynthMouseMoveEvent(aEvent, guid, blockId)
              : SendNormalPrioritySynthMouseMoveEvent(aEvent, guid, blockId);
      NS_WARNING_ASSERTION(ret, "SendSynthMouseMoveEvent() failed");
      MOZ_ASSERT(!ret || aEvent.HasBeenPostedToRemoteProcess());
      return;
    }
    DebugOnly<bool> ret =
        isInputPriorityEventEnabled
            ? SendRealMouseMoveEvent(aEvent, guid, blockId)
            : SendNormalPriorityRealMouseMoveEvent(aEvent, guid, blockId);
    NS_WARNING_ASSERTION(ret, "SendRealMouseMoveEvent() failed");
    MOZ_ASSERT(!ret || aEvent.HasBeenPostedToRemoteProcess());
    return;
  }

  DebugOnly<bool> ret =
      isInputPriorityEventEnabled
          ? SendRealMouseButtonEvent(aEvent, guid, blockId)
          : SendNormalPriorityRealMouseButtonEvent(aEvent, guid, blockId);
  NS_WARNING_ASSERTION(ret, "SendRealMouseButtonEvent() failed");
  MOZ_ASSERT(!ret || aEvent.HasBeenPostedToRemoteProcess());
}

LayoutDeviceToCSSScale BrowserParent::GetLayoutDeviceToCSSScale() {
  Document* doc = (mFrameElement ? mFrameElement->OwnerDoc() : nullptr);
  nsPresContext* ctx = (doc ? doc->GetPresContext() : nullptr);
  return LayoutDeviceToCSSScale(
      ctx ? (float)ctx->AppUnitsPerDevPixel() / AppUnitsPerCSSPixel() : 0.0f);
}

bool BrowserParent::QueryDropLinksForVerification() {
  // Before sending the dragEvent, we query the links being dragged and
  // store them on the parent, to make sure the child can not modify links.
  nsCOMPtr<nsIDragSession> dragSession = nsContentUtils::GetDragSession();
  if (!dragSession) {
    NS_WARNING("No dragSession to query links for verification");
    return false;
  }

  RefPtr<DataTransfer> initialDataTransfer = dragSession->GetDataTransfer();
  if (!initialDataTransfer) {
    NS_WARNING("No initialDataTransfer to query links for verification");
    return false;
  }

  nsCOMPtr<nsIDroppedLinkHandler> dropHandler =
      do_GetService("@mozilla.org/content/dropped-link-handler;1");
  if (!dropHandler) {
    NS_WARNING("No dropHandler to query links for verification");
    return false;
  }

  // No more than one drop event can happen simultaneously; reset the link
  // verification array and store all links that are being dragged.
  mVerifyDropLinks.Clear();

  nsTArray<RefPtr<nsIDroppedLinkItem>> droppedLinkItems;
  dropHandler->QueryLinks(initialDataTransfer, droppedLinkItems);

  // Since the entire event is cancelled if one of the links is invalid,
  // we can store all links on the parent side without any prior
  // validation checks.
  nsresult rv = NS_OK;
  for (nsIDroppedLinkItem* item : droppedLinkItems) {
    nsString tmp;
    rv = item->GetUrl(tmp);
    if (NS_FAILED(rv)) {
      NS_WARNING("Failed to query url for verification");
      break;
    }
    mVerifyDropLinks.AppendElement(tmp);

    rv = item->GetName(tmp);
    if (NS_FAILED(rv)) {
      NS_WARNING("Failed to query name for verification");
      break;
    }
    mVerifyDropLinks.AppendElement(tmp);

    rv = item->GetType(tmp);
    if (NS_FAILED(rv)) {
      NS_WARNING("Failed to query type for verification");
      break;
    }
    mVerifyDropLinks.AppendElement(tmp);
  }
  if (NS_FAILED(rv)) {
    mVerifyDropLinks.Clear();
    return false;
  }
  return true;
}

void BrowserParent::SendRealDragEvent(WidgetDragEvent& aEvent,
                                      uint32_t aDragAction,
                                      uint32_t aDropEffect,
                                      nsIPrincipal* aPrincipal) {
  if (mIsDestroyed || !mIsReadyToHandleInputEvents) {
    return;
  }
  MOZ_ASSERT(!Manager()->IsInputPriorityEventEnabled());
  aEvent.mRefPoint = TransformParentToChild(aEvent.mRefPoint);
  if (aEvent.mMessage == eDrop) {
    if (!QueryDropLinksForVerification()) {
      return;
    }
  }
  DebugOnly<bool> ret = PBrowserParent::SendRealDragEvent(
      aEvent, aDragAction, aDropEffect, aPrincipal);
  NS_WARNING_ASSERTION(ret, "PBrowserParent::SendRealDragEvent() failed");
  MOZ_ASSERT(!ret || aEvent.HasBeenPostedToRemoteProcess());
}

void BrowserParent::SendMouseWheelEvent(WidgetWheelEvent& aEvent) {
  if (mIsDestroyed || !mIsReadyToHandleInputEvents) {
    return;
  }

  ScrollableLayerGuid guid;
  uint64_t blockId;
  ApzAwareEventRoutingToChild(&guid, &blockId, nullptr);
  aEvent.mRefPoint = TransformParentToChild(aEvent.mRefPoint);
  DebugOnly<bool> ret =
      Manager()->IsInputPriorityEventEnabled()
          ? PBrowserParent::SendMouseWheelEvent(aEvent, guid, blockId)
          : PBrowserParent::SendNormalPriorityMouseWheelEvent(aEvent, guid,
                                                              blockId);

  NS_WARNING_ASSERTION(ret, "PBrowserParent::SendMouseWheelEvent() failed");
  MOZ_ASSERT(!ret || aEvent.HasBeenPostedToRemoteProcess());
}

mozilla::ipc::IPCResult BrowserParent::RecvDispatchWheelEvent(
    const mozilla::WidgetWheelEvent& aEvent) {
  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (!widget) {
    return IPC_OK();
  }

  WidgetWheelEvent localEvent(aEvent);
  localEvent.mWidget = widget;
  localEvent.mRefPoint = TransformChildToParent(localEvent.mRefPoint);

  widget->DispatchInputEvent(&localEvent);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvDispatchMouseEvent(
    const mozilla::WidgetMouseEvent& aEvent) {
  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (!widget) {
    return IPC_OK();
  }

  WidgetMouseEvent localEvent(aEvent);
  localEvent.mWidget = widget;
  localEvent.mRefPoint = TransformChildToParent(localEvent.mRefPoint);

  widget->DispatchInputEvent(&localEvent);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvDispatchKeyboardEvent(
    const mozilla::WidgetKeyboardEvent& aEvent) {
  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (!widget) {
    return IPC_OK();
  }

  WidgetKeyboardEvent localEvent(aEvent);
  localEvent.mWidget = widget;
  localEvent.mRefPoint = TransformChildToParent(localEvent.mRefPoint);

  widget->DispatchInputEvent(&localEvent);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvRequestNativeKeyBindings(
    const uint32_t& aType, const WidgetKeyboardEvent& aEvent,
    nsTArray<CommandInt>* aCommands) {
  MOZ_ASSERT(aCommands);
  MOZ_ASSERT(aCommands->IsEmpty());

  nsIWidget::NativeKeyBindingsType keyBindingsType =
      static_cast<nsIWidget::NativeKeyBindingsType>(aType);
  switch (keyBindingsType) {
    case nsIWidget::NativeKeyBindingsForSingleLineEditor:
    case nsIWidget::NativeKeyBindingsForMultiLineEditor:
    case nsIWidget::NativeKeyBindingsForRichTextEditor:
      break;
    default:
      return IPC_FAIL(this, "Invalid aType value");
  }

  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (!widget) {
    return IPC_OK();
  }

  WidgetKeyboardEvent localEvent(aEvent);
  localEvent.mWidget = widget;

  if (NS_FAILED(widget->AttachNativeKeyEvent(localEvent))) {
    return IPC_OK();
  }

  if (localEvent.InitEditCommandsFor(keyBindingsType)) {
    *aCommands = localEvent.EditCommandsConstRef(keyBindingsType);
  }

  return IPC_OK();
}

class SynthesizedEventObserver : public nsIObserver {
  NS_DECL_ISUPPORTS

 public:
  SynthesizedEventObserver(BrowserParent* aBrowserParent,
                           const uint64_t& aObserverId)
      : mBrowserParent(aBrowserParent), mObserverId(aObserverId) {
    MOZ_ASSERT(mBrowserParent);
  }

  NS_IMETHOD Observe(nsISupports* aSubject, const char* aTopic,
                     const char16_t* aData) override {
    if (!mBrowserParent || !mObserverId) {
      // We already sent the notification, or we don't actually need to
      // send any notification at all.
      return NS_OK;
    }

    if (mBrowserParent->IsDestroyed()) {
      // If this happens it's probably a bug in the test that's triggering this.
      NS_WARNING(
          "BrowserParent was unexpectedly destroyed during event "
          "synthesization!");
    } else if (!mBrowserParent->SendNativeSynthesisResponse(
                   mObserverId, nsCString(aTopic))) {
      NS_WARNING("Unable to send native event synthesization response!");
    }
    // Null out browserParent to indicate we already sent the response
    mBrowserParent = nullptr;
    return NS_OK;
  }

 private:
  virtual ~SynthesizedEventObserver() {}

  RefPtr<BrowserParent> mBrowserParent;
  uint64_t mObserverId;
};

NS_IMPL_ISUPPORTS(SynthesizedEventObserver, nsIObserver)

class MOZ_STACK_CLASS AutoSynthesizedEventResponder {
 public:
  AutoSynthesizedEventResponder(BrowserParent* aBrowserParent,
                                const uint64_t& aObserverId, const char* aTopic)
      : mObserver(new SynthesizedEventObserver(aBrowserParent, aObserverId)),
        mTopic(aTopic) {}

  ~AutoSynthesizedEventResponder() {
    // This may be a no-op if the observer already sent a response.
    mObserver->Observe(nullptr, mTopic, nullptr);
  }

  nsIObserver* GetObserver() { return mObserver; }

 private:
  nsCOMPtr<nsIObserver> mObserver;
  const char* mTopic;
};

mozilla::ipc::IPCResult BrowserParent::RecvSynthesizeNativeKeyEvent(
    const int32_t& aNativeKeyboardLayout, const int32_t& aNativeKeyCode,
    const uint32_t& aModifierFlags, const nsString& aCharacters,
    const nsString& aUnmodifiedCharacters, const uint64_t& aObserverId) {
  AutoSynthesizedEventResponder responder(this, aObserverId, "keyevent");
  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (widget) {
    widget->SynthesizeNativeKeyEvent(
        aNativeKeyboardLayout, aNativeKeyCode, aModifierFlags, aCharacters,
        aUnmodifiedCharacters, responder.GetObserver());
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvSynthesizeNativeMouseEvent(
    const LayoutDeviceIntPoint& aPoint, const uint32_t& aNativeMessage,
    const uint32_t& aModifierFlags, const uint64_t& aObserverId) {
  AutoSynthesizedEventResponder responder(this, aObserverId, "mouseevent");
  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (widget) {
    widget->SynthesizeNativeMouseEvent(aPoint, aNativeMessage, aModifierFlags,
                                       responder.GetObserver());
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvSynthesizeNativeMouseMove(
    const LayoutDeviceIntPoint& aPoint, const uint64_t& aObserverId) {
  AutoSynthesizedEventResponder responder(this, aObserverId, "mousemove");
  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (widget) {
    widget->SynthesizeNativeMouseMove(aPoint, responder.GetObserver());
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvSynthesizeNativeMouseScrollEvent(
    const LayoutDeviceIntPoint& aPoint, const uint32_t& aNativeMessage,
    const double& aDeltaX, const double& aDeltaY, const double& aDeltaZ,
    const uint32_t& aModifierFlags, const uint32_t& aAdditionalFlags,
    const uint64_t& aObserverId) {
  AutoSynthesizedEventResponder responder(this, aObserverId,
                                          "mousescrollevent");
  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (widget) {
    widget->SynthesizeNativeMouseScrollEvent(
        aPoint, aNativeMessage, aDeltaX, aDeltaY, aDeltaZ, aModifierFlags,
        aAdditionalFlags, responder.GetObserver());
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvSynthesizeNativeTouchPoint(
    const uint32_t& aPointerId, const TouchPointerState& aPointerState,
    const LayoutDeviceIntPoint& aPoint, const double& aPointerPressure,
    const uint32_t& aPointerOrientation, const uint64_t& aObserverId) {
  AutoSynthesizedEventResponder responder(this, aObserverId, "touchpoint");
  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (widget) {
    widget->SynthesizeNativeTouchPoint(aPointerId, aPointerState, aPoint,
                                       aPointerPressure, aPointerOrientation,
                                       responder.GetObserver());
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvSynthesizeNativeTouchTap(
    const LayoutDeviceIntPoint& aPoint, const bool& aLongTap,
    const uint64_t& aObserverId) {
  AutoSynthesizedEventResponder responder(this, aObserverId, "touchtap");
  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (widget) {
    widget->SynthesizeNativeTouchTap(aPoint, aLongTap, responder.GetObserver());
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvClearNativeTouchSequence(
    const uint64_t& aObserverId) {
  AutoSynthesizedEventResponder responder(this, aObserverId, "cleartouch");
  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (widget) {
    widget->ClearNativeTouchSequence(responder.GetObserver());
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult
BrowserParent::RecvSetPrefersReducedMotionOverrideForTest(const bool& aValue) {
  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (widget) {
    widget->SetPrefersReducedMotionOverrideForTest(aValue);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult
BrowserParent::RecvResetPrefersReducedMotionOverrideForTest() {
  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (widget) {
    widget->ResetPrefersReducedMotionOverrideForTest();
  }
  return IPC_OK();
}

void BrowserParent::SendRealKeyEvent(WidgetKeyboardEvent& aEvent) {
  if (mIsDestroyed || !mIsReadyToHandleInputEvents) {
    return;
  }
  aEvent.mRefPoint = TransformParentToChild(aEvent.mRefPoint);

  if (aEvent.mMessage == eKeyPress) {
    // XXX Should we do this only when input context indicates an editor having
    //     focus and the key event won't cause inputting text?
    aEvent.InitAllEditCommands();
  } else {
    aEvent.PreventNativeKeyBindings();
  }
  DebugOnly<bool> ret =
      Manager()->IsInputPriorityEventEnabled()
          ? PBrowserParent::SendRealKeyEvent(aEvent)
          : PBrowserParent::SendNormalPriorityRealKeyEvent(aEvent);

  NS_WARNING_ASSERTION(ret, "PBrowserParent::SendRealKeyEvent() failed");
  MOZ_ASSERT(!ret || aEvent.HasBeenPostedToRemoteProcess());
}

void BrowserParent::SendRealTouchEvent(WidgetTouchEvent& aEvent) {
  if (mIsDestroyed || !mIsReadyToHandleInputEvents) {
    return;
  }

  // PresShell::HandleEventInternal adds touches on touch end/cancel.  This
  // confuses remote content and the panning and zooming logic into thinking
  // that the added touches are part of the touchend/cancel, when actually
  // they're not.
  if (aEvent.mMessage == eTouchEnd || aEvent.mMessage == eTouchCancel) {
    aEvent.mTouches.RemoveElementsBy(
        [](const auto& touch) { return !touch->mChanged; });
  }

  ScrollableLayerGuid guid;
  uint64_t blockId;
  nsEventStatus apzResponse;
  ApzAwareEventRoutingToChild(&guid, &blockId, &apzResponse);

  if (mIsDestroyed) {
    return;
  }

  for (uint32_t i = 0; i < aEvent.mTouches.Length(); i++) {
    aEvent.mTouches[i]->mRefPoint =
        TransformParentToChild(aEvent.mTouches[i]->mRefPoint);
  }

  bool inputPriorityEventEnabled = Manager()->IsInputPriorityEventEnabled();

  if (aEvent.mMessage == eTouchMove) {
    DebugOnly<bool> ret =
        inputPriorityEventEnabled
            ? PBrowserParent::SendRealTouchMoveEvent(aEvent, guid, blockId,
                                                     apzResponse)
            : PBrowserParent::SendNormalPriorityRealTouchMoveEvent(
                  aEvent, guid, blockId, apzResponse);

    NS_WARNING_ASSERTION(ret,
                         "PBrowserParent::SendRealTouchMoveEvent() failed");
    MOZ_ASSERT(!ret || aEvent.HasBeenPostedToRemoteProcess());
    return;
  }
  DebugOnly<bool> ret = inputPriorityEventEnabled
                            ? PBrowserParent::SendRealTouchEvent(
                                  aEvent, guid, blockId, apzResponse)
                            : PBrowserParent::SendNormalPriorityRealTouchEvent(
                                  aEvent, guid, blockId, apzResponse);

  NS_WARNING_ASSERTION(ret, "PBrowserParent::SendRealTouchEvent() failed");
  MOZ_ASSERT(!ret || aEvent.HasBeenPostedToRemoteProcess());
}

void BrowserParent::SendPluginEvent(WidgetPluginEvent& aEvent) {
  DebugOnly<bool> ret = PBrowserParent::SendPluginEvent(aEvent);
  NS_WARNING_ASSERTION(ret, "PBrowserParent::SendPluginEvent() failed");
  MOZ_ASSERT(!ret || aEvent.HasBeenPostedToRemoteProcess());
}

bool BrowserParent::SendHandleTap(TapType aType,
                                  const LayoutDevicePoint& aPoint,
                                  Modifiers aModifiers,
                                  const ScrollableLayerGuid& aGuid,
                                  uint64_t aInputBlockId) {
  if (mIsDestroyed || !mIsReadyToHandleInputEvents) {
    return false;
  }
  if ((aType == TapType::eSingleTap || aType == TapType::eSecondTap)) {
    nsFocusManager* fm = nsFocusManager::GetFocusManager();
    if (fm) {
      RefPtr<nsFrameLoader> frameLoader = GetFrameLoader();
      if (frameLoader) {
        RefPtr<Element> element = frameLoader->GetOwnerContent();
        if (element) {
          fm->SetFocus(element, nsIFocusManager::FLAG_BYMOUSE |
                                    nsIFocusManager::FLAG_BYTOUCH |
                                    nsIFocusManager::FLAG_NOSCROLL);
        }
      }
    }
  }
  return Manager()->IsInputPriorityEventEnabled()
             ? PBrowserParent::SendHandleTap(aType,
                                             TransformParentToChild(aPoint),
                                             aModifiers, aGuid, aInputBlockId)
             : PBrowserParent::SendNormalPriorityHandleTap(
                   aType, TransformParentToChild(aPoint), aModifiers, aGuid,
                   aInputBlockId);
}

mozilla::ipc::IPCResult BrowserParent::RecvSyncMessage(
    const nsString& aMessage, const ClonedMessageData& aData,
    nsTArray<StructuredCloneData>* aRetVal) {
  AUTO_PROFILER_LABEL_DYNAMIC_LOSSY_NSSTRING("BrowserParent::RecvSyncMessage",
                                             OTHER, aMessage);
  MMPrinter::Print("BrowserParent::RecvSyncMessage", aMessage, aData);

  StructuredCloneData data;
  ipc::UnpackClonedMessageDataForParent(aData, data);

  if (!ReceiveMessage(aMessage, true, &data, aRetVal)) {
    return IPC_FAIL_NO_REASON(this);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvAsyncMessage(
    const nsString& aMessage, const ClonedMessageData& aData) {
  AUTO_PROFILER_LABEL_DYNAMIC_LOSSY_NSSTRING("BrowserParent::RecvAsyncMessage",
                                             OTHER, aMessage);
  MMPrinter::Print("BrowserParent::RecvAsyncMessage", aMessage, aData);

  StructuredCloneData data;
  ipc::UnpackClonedMessageDataForParent(aData, data);

  if (!ReceiveMessage(aMessage, false, &data, nullptr)) {
    return IPC_FAIL_NO_REASON(this);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvSetCursor(
    const nsCursor& aCursor, const bool& aHasCustomCursor,
    const nsCString& aCursorData, const uint32_t& aWidth,
    const uint32_t& aHeight, const uint32_t& aStride,
    const gfx::SurfaceFormat& aFormat, const uint32_t& aHotspotX,
    const uint32_t& aHotspotY, const bool& aForce) {
  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (!widget) {
    return IPC_OK();
  }

  if (aForce) {
    widget->ClearCachedCursor();
  }

  if (!mTabSetsCursor) {
    return IPC_OK();
  }

  nsCOMPtr<imgIContainer> cursorImage;
  if (aHasCustomCursor) {
    if (aHeight * aStride != aCursorData.Length() ||
        aStride < aWidth * gfx::BytesPerPixel(aFormat)) {
      return IPC_FAIL(this, "Invalid custom cursor data");
    }
    const gfx::IntSize size(aWidth, aHeight);
    RefPtr<gfx::DataSourceSurface> customCursor =
        gfx::CreateDataSourceSurfaceFromData(
            size, aFormat,
            reinterpret_cast<const uint8_t*>(aCursorData.BeginReading()),
            aStride);

    RefPtr<gfxDrawable> drawable = new gfxSurfaceDrawable(customCursor, size);
    cursorImage = image::ImageOps::CreateFromDrawable(drawable);
  }

  widget->SetCursor(aCursor, cursorImage, aHotspotX, aHotspotY);
  mCursor = aCursor;
  mCustomCursor = cursorImage;
  mCustomCursorHotspotX = aHotspotX;
  mCustomCursorHotspotY = aHotspotY;

  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvSetLinkStatus(
    const nsString& aStatus) {
  nsCOMPtr<nsIXULBrowserWindow> xulBrowserWindow = GetXULBrowserWindow();
  if (!xulBrowserWindow) {
    return IPC_OK();
  }

  xulBrowserWindow->SetOverLink(aStatus);

  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvShowTooltip(
    const uint32_t& aX, const uint32_t& aY, const nsString& aTooltip,
    const nsString& aDirection) {
  nsCOMPtr<nsIXULBrowserWindow> xulBrowserWindow = GetXULBrowserWindow();
  if (!xulBrowserWindow) {
    return IPC_OK();
  }

  // ShowTooltip will end up accessing XULElement properties in JS (specifically
  // BoxObject). However, to get it to JS, we need to make sure we're a
  // nsFrameLoaderOwner, which implies we're a XULFrameElement. We can then
  // safely pass Element into JS.
  RefPtr<nsFrameLoaderOwner> flo = do_QueryObject(mFrameElement);
  if (!flo) return IPC_OK();

  nsCOMPtr<Element> el = do_QueryInterface(flo);
  if (!el) return IPC_OK();

  xulBrowserWindow->ShowTooltip(aX, aY, aTooltip, aDirection, el);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvHideTooltip() {
  nsCOMPtr<nsIXULBrowserWindow> xulBrowserWindow = GetXULBrowserWindow();
  if (!xulBrowserWindow) {
    return IPC_OK();
  }

  xulBrowserWindow->HideTooltip();
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvNotifyIMEFocus(
    const ContentCache& aContentCache, const IMENotification& aIMENotification,
    NotifyIMEFocusResolver&& aResolve) {
  if (mIsDestroyed) {
    return IPC_OK();
  }

  nsCOMPtr<nsIWidget> widget = GetTextInputHandlingWidget();
  if (!widget) {
    aResolve(IMENotificationRequests());
    return IPC_OK();
  }

  mContentCache.AssignContent(aContentCache, widget, &aIMENotification);
  IMEStateManager::NotifyIME(aIMENotification, widget, this);

  IMENotificationRequests requests;
  if (aIMENotification.mMessage == NOTIFY_IME_OF_FOCUS) {
    requests = widget->IMENotificationRequestsRef();
  }
  aResolve(requests);

  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvNotifyIMETextChange(
    const ContentCache& aContentCache,
    const IMENotification& aIMENotification) {
  nsCOMPtr<nsIWidget> widget = GetTextInputHandlingWidget();
  if (!widget || !IMEStateManager::DoesBrowserParentHaveIMEFocus(this)) {
    return IPC_OK();
  }
  mContentCache.AssignContent(aContentCache, widget, &aIMENotification);
  mContentCache.MaybeNotifyIME(widget, aIMENotification);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvNotifyIMECompositionUpdate(
    const ContentCache& aContentCache,
    const IMENotification& aIMENotification) {
  nsCOMPtr<nsIWidget> widget = GetTextInputHandlingWidget();
  if (!widget || !IMEStateManager::DoesBrowserParentHaveIMEFocus(this)) {
    return IPC_OK();
  }
  mContentCache.AssignContent(aContentCache, widget, &aIMENotification);
  mContentCache.MaybeNotifyIME(widget, aIMENotification);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvNotifyIMESelection(
    const ContentCache& aContentCache,
    const IMENotification& aIMENotification) {
  nsCOMPtr<nsIWidget> widget = GetTextInputHandlingWidget();
  if (!widget || !IMEStateManager::DoesBrowserParentHaveIMEFocus(this)) {
    return IPC_OK();
  }
  mContentCache.AssignContent(aContentCache, widget, &aIMENotification);
  mContentCache.MaybeNotifyIME(widget, aIMENotification);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvUpdateContentCache(
    const ContentCache& aContentCache) {
  nsCOMPtr<nsIWidget> widget = GetTextInputHandlingWidget();
  if (!widget || !IMEStateManager::DoesBrowserParentHaveIMEFocus(this)) {
    return IPC_OK();
  }

  mContentCache.AssignContent(aContentCache, widget);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvNotifyIMEMouseButtonEvent(
    const IMENotification& aIMENotification, bool* aConsumedByIME) {
  nsCOMPtr<nsIWidget> widget = GetTextInputHandlingWidget();
  if (!widget || !IMEStateManager::DoesBrowserParentHaveIMEFocus(this)) {
    *aConsumedByIME = false;
    return IPC_OK();
  }
  nsresult rv = IMEStateManager::NotifyIME(aIMENotification, widget, this);
  *aConsumedByIME = rv == NS_SUCCESS_EVENT_CONSUMED;
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvNotifyIMEPositionChange(
    const ContentCache& aContentCache,
    const IMENotification& aIMENotification) {
  nsCOMPtr<nsIWidget> widget = GetTextInputHandlingWidget();
  if (!widget || !IMEStateManager::DoesBrowserParentHaveIMEFocus(this)) {
    return IPC_OK();
  }
  mContentCache.AssignContent(aContentCache, widget, &aIMENotification);
  mContentCache.MaybeNotifyIME(widget, aIMENotification);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvOnEventNeedingAckHandled(
    const EventMessage& aMessage) {
  // This is called when the child process receives WidgetCompositionEvent or
  // WidgetSelectionEvent.
  // FYI: Don't check if widget is nullptr here because it's more important to
  //      notify mContentCahce of this than handling something in it.
  nsCOMPtr<nsIWidget> widget = GetTextInputHandlingWidget();

  // While calling OnEventNeedingAckHandled(), BrowserParent *might* be
  // destroyed since it may send notifications to IME.
  RefPtr<BrowserParent> kungFuDeathGrip(this);
  mContentCache.OnEventNeedingAckHandled(widget, aMessage);
  return IPC_OK();
}

void BrowserParent::HandledWindowedPluginKeyEvent(
    const NativeEventData& aKeyEventData, bool aIsConsumed) {
  DebugOnly<bool> ok =
      SendHandledWindowedPluginKeyEvent(aKeyEventData, aIsConsumed);
  NS_WARNING_ASSERTION(ok, "SendHandledWindowedPluginKeyEvent failed");
}

mozilla::ipc::IPCResult BrowserParent::RecvOnWindowedPluginKeyEvent(
    const NativeEventData& aKeyEventData) {
  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (NS_WARN_IF(!widget)) {
    // Notifies the plugin process of the key event being not consumed by us.
    HandledWindowedPluginKeyEvent(aKeyEventData, false);
    return IPC_OK();
  }
  nsresult rv = widget->OnWindowedPluginKeyEvent(aKeyEventData, this);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    // Notifies the plugin process of the key event being not consumed by us.
    HandledWindowedPluginKeyEvent(aKeyEventData, false);
    return IPC_OK();
  }

  // If the key event is posted to another process, we need to wait a call
  // of HandledWindowedPluginKeyEvent().  So, nothing to do here in this case.
  if (rv == NS_SUCCESS_EVENT_HANDLED_ASYNCHRONOUSLY) {
    return IPC_OK();
  }

  // Otherwise, the key event is handled synchronously.  Let's notify the
  // plugin process of the key event's result.
  bool consumed = (rv == NS_SUCCESS_EVENT_CONSUMED);
  HandledWindowedPluginKeyEvent(aKeyEventData, consumed);

  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvRequestFocus(const bool& aCanRaise) {
  LOGBROWSERFOCUS(("RecvRequestFocus %p, aCanRaise: %d", this, aCanRaise));
  if (BrowserBridgeParent* bridgeParent = GetBrowserBridgeParent()) {
    mozilla::Unused << bridgeParent->SendRequestFocus(aCanRaise);
    return IPC_OK();
  }

  if (!mFrameElement) {
    return IPC_OK();
  }

  nsContentUtils::RequestFrameFocus(*mFrameElement, aCanRaise);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvEnableDisableCommands(
    const nsString& aAction, nsTArray<nsCString>&& aEnabledCommands,
    nsTArray<nsCString>&& aDisabledCommands) {
  nsCOMPtr<nsIBrowser> browser =
      mFrameElement ? mFrameElement->AsBrowser() : nullptr;
  bool isRemoteBrowser = false;
  if (browser) {
    browser->GetIsRemoteBrowser(&isRemoteBrowser);
  }
  if (isRemoteBrowser) {
    browser->EnableDisableCommandsRemoteOnly(aAction, aEnabledCommands,
                                             aDisabledCommands);
  }

  return IPC_OK();
}

LayoutDeviceIntPoint BrowserParent::TransformPoint(
    const LayoutDeviceIntPoint& aPoint,
    const LayoutDeviceToLayoutDeviceMatrix4x4& aMatrix) {
  LayoutDevicePoint floatPoint(aPoint);
  LayoutDevicePoint floatTransformed = TransformPoint(floatPoint, aMatrix);
  // The next line loses precision if an out-of-process iframe
  // has been scaled or rotated.
  return RoundedToInt(floatTransformed);
}

LayoutDevicePoint BrowserParent::TransformPoint(
    const LayoutDevicePoint& aPoint,
    const LayoutDeviceToLayoutDeviceMatrix4x4& aMatrix) {
  return aMatrix.TransformPoint(aPoint);
}

LayoutDeviceIntPoint BrowserParent::TransformParentToChild(
    const LayoutDeviceIntPoint& aPoint) {
  LayoutDeviceToLayoutDeviceMatrix4x4 matrix =
      GetChildToParentConversionMatrix();
  if (!matrix.Invert()) {
    return LayoutDeviceIntPoint(0, 0);
  }
  return TransformPoint(aPoint, matrix);
}

LayoutDevicePoint BrowserParent::TransformParentToChild(
    const LayoutDevicePoint& aPoint) {
  LayoutDeviceToLayoutDeviceMatrix4x4 matrix =
      GetChildToParentConversionMatrix();
  if (!matrix.Invert()) {
    return LayoutDevicePoint(0.0, 0.0);
  }
  return TransformPoint(aPoint, matrix);
}

LayoutDeviceIntPoint BrowserParent::TransformChildToParent(
    const LayoutDeviceIntPoint& aPoint) {
  return TransformPoint(aPoint, GetChildToParentConversionMatrix());
}

LayoutDevicePoint BrowserParent::TransformChildToParent(
    const LayoutDevicePoint& aPoint) {
  return TransformPoint(aPoint, GetChildToParentConversionMatrix());
}

LayoutDeviceIntRect BrowserParent::TransformChildToParent(
    const LayoutDeviceIntRect& aRect) {
  LayoutDeviceToLayoutDeviceMatrix4x4 matrix =
      GetChildToParentConversionMatrix();
  LayoutDeviceRect floatRect(aRect);
  // The outcome is not ideal if an out-of-process iframe has been rotated
  LayoutDeviceRect floatTransformed = matrix.TransformBounds(floatRect);
  // The next line loses precision if an out-of-process iframe
  // has been scaled or rotated.
  return RoundedToInt(floatTransformed);
}

LayoutDeviceToLayoutDeviceMatrix4x4
BrowserParent::GetChildToParentConversionMatrix() {
  if (mChildToParentConversionMatrix) {
    return *mChildToParentConversionMatrix;
  }
  LayoutDevicePoint offset(-GetChildProcessOffset());
  return LayoutDeviceToLayoutDeviceMatrix4x4::Translation(offset);
}

void BrowserParent::SetChildToParentConversionMatrix(
    const Maybe<LayoutDeviceToLayoutDeviceMatrix4x4>& aMatrix) {
  mChildToParentConversionMatrix = aMatrix;
  if (mIsDestroyed) {
    return;
  }
  mozilla::Unused << SendChildToParentMatrix(ToUnknownMatrix(aMatrix));
}

LayoutDeviceIntPoint BrowserParent::GetChildProcessOffset() {
  // The "toplevel widget" in child processes is always at position
  // 0,0.  Map the event coordinates to match that.

  LayoutDeviceIntPoint offset(0, 0);
  RefPtr<nsFrameLoader> frameLoader = GetFrameLoader();
  if (!frameLoader) {
    return offset;
  }
  nsIFrame* targetFrame = frameLoader->GetPrimaryFrameOfOwningContent();
  if (!targetFrame) {
    return offset;
  }

  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (!widget) {
    return offset;
  }

  nsPresContext* presContext = targetFrame->PresContext();
  nsIFrame* rootFrame = presContext->PresShell()->GetRootFrame();
  nsView* rootView = rootFrame ? rootFrame->GetView() : nullptr;
  if (!rootView) {
    return offset;
  }

  // Note that we don't want to take into account transforms here:
#if 0
  nsPoint pt(0, 0);
  nsLayoutUtils::TransformPoint(targetFrame, rootFrame, pt);
#endif
  // In practice, when transforms are applied to this frameLoader, we currently
  // get the wrong results whether we take transforms into account here or not.
  // But applying transforms here gives us the wrong results in all
  // circumstances when transforms are applied, unless they're purely
  // translational. It also gives us the wrong results whenever CSS transitions
  // are used to apply transforms, since the offeets aren't updated as the
  // transition is animated.
  //
  // What we actually need to do is apply the transforms to the coordinates of
  // any events we send to the child, and reverse them for any screen
  // coordinates that we retrieve from the child.

  // TODO: Once we take into account transforms here, set viewportType
  // correctly. For now we use Visual as this means we don't apply
  // the layout-to-visual transform in TranslateViewToWidget().
  ViewportType viewportType = ViewportType::Visual;

  nsPoint pt = targetFrame->GetOffsetTo(rootFrame);
  return -nsLayoutUtils::TranslateViewToWidget(presContext, rootView, pt,
                                               viewportType, widget);
}

LayoutDeviceIntPoint BrowserParent::GetClientOffset() {
  nsCOMPtr<nsIWidget> widget = GetWidget();
  nsCOMPtr<nsIWidget> docWidget = GetDocWidget();

  if (widget == docWidget) {
    return widget->GetClientOffset();
  }

  return (docWidget->GetClientOffset() +
          nsLayoutUtils::WidgetToWidgetOffset(widget, docWidget));
}

void BrowserParent::StopIMEStateManagement() {
  if (mIsDestroyed) {
    return;
  }
  Unused << SendStopIMEStateManagement();
}

mozilla::ipc::IPCResult BrowserParent::RecvReplyKeyEvent(
    const WidgetKeyboardEvent& aEvent) {
  NS_ENSURE_TRUE(mFrameElement, IPC_OK());

  WidgetKeyboardEvent localEvent(aEvent);
  localEvent.MarkAsHandledInRemoteProcess();

  // Here we convert the WidgetEvent that we received to an Event
  // to be able to dispatch it to the <browser> element as the target element.
  Document* doc = mFrameElement->OwnerDoc();
  nsPresContext* presContext = doc->GetPresContext();
  NS_ENSURE_TRUE(presContext, IPC_OK());

  AutoHandlingUserInputStatePusher userInpStatePusher(localEvent.IsTrusted(),
                                                      &localEvent);

  nsEventStatus status = nsEventStatus_eIgnore;

  // Handle access key in this process before dispatching reply event because
  // ESM handles it before dispatching the event to the DOM tree.
  if (localEvent.mMessage == eKeyPress &&
      (localEvent.ModifiersMatchWithAccessKey(AccessKeyType::eChrome) ||
       localEvent.ModifiersMatchWithAccessKey(AccessKeyType::eContent))) {
    RefPtr<EventStateManager> esm = presContext->EventStateManager();
    AutoTArray<uint32_t, 10> accessCharCodes;
    localEvent.GetAccessKeyCandidates(accessCharCodes);
    if (esm->HandleAccessKey(&localEvent, presContext, accessCharCodes)) {
      status = nsEventStatus_eConsumeNoDefault;
    }
  }

  EventDispatcher::Dispatch(mFrameElement, presContext, &localEvent, nullptr,
                            &status);

  if (!localEvent.DefaultPrevented() &&
      !localEvent.mFlags.mIsSynthesizedForTests) {
    nsCOMPtr<nsIWidget> widget = GetWidget();
    if (widget) {
      widget->PostHandleKeyEvent(&localEvent);
      localEvent.StopPropagation();
    }
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvAccessKeyNotHandled(
    const WidgetKeyboardEvent& aEvent) {
  NS_ENSURE_TRUE(mFrameElement, IPC_OK());

  // This is called only when this process had focus and HandleAccessKey
  // message was posted to all remote process and each remote process didn't
  // execute any content access keys.
  // XXX If there were two or more remote processes, this may be called
  //     twice or more for a keyboard event, that must be a bug.  But how to
  //     detect if received event has already been handled?

  MOZ_ASSERT(aEvent.mMessage == eKeyPress);
  WidgetKeyboardEvent localEvent(aEvent);
  localEvent.MarkAsHandledInRemoteProcess();
  localEvent.mMessage = eAccessKeyNotFound;

  // Here we convert the WidgetEvent that we received to an Event
  // to be able to dispatch it to the <browser> element as the target element.
  Document* doc = mFrameElement->OwnerDoc();
  PresShell* presShell = doc->GetPresShell();
  NS_ENSURE_TRUE(presShell, IPC_OK());

  if (presShell->CanDispatchEvent()) {
    nsPresContext* presContext = presShell->GetPresContext();
    NS_ENSURE_TRUE(presContext, IPC_OK());

    EventDispatcher::Dispatch(mFrameElement, presContext, &localEvent);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvSetHasBeforeUnload(
    const bool& aHasBeforeUnload) {
  mHasBeforeUnload = aHasBeforeUnload;
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvRegisterProtocolHandler(
    const nsString& aScheme, nsIURI* aHandlerURI, const nsString& aTitle,
    nsIURI* aDocURI) {
  nsCOMPtr<nsIWebProtocolHandlerRegistrar> registrar =
      do_GetService(NS_WEBPROTOCOLHANDLERREGISTRAR_CONTRACTID);
  if (registrar) {
    registrar->RegisterProtocolHandler(aScheme, aHandlerURI, aTitle, aDocURI,
                                       mFrameElement);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvOnStateChange(
    const Maybe<WebProgressData>& aWebProgressData,
    const RequestData& aRequestData, const uint32_t aStateFlags,
    const nsresult aStatus,
    const Maybe<WebProgressStateChangeData>& aStateChangeData) {
  nsCOMPtr<nsIBrowser> browser;
  nsCOMPtr<nsIWebProgress> manager;
  nsCOMPtr<nsIWebProgressListener> managerAsListener;
  if (!GetWebProgressListener(getter_AddRefs(browser), getter_AddRefs(manager),
                              getter_AddRefs(managerAsListener))) {
    return IPC_OK();
  }

  nsCOMPtr<nsIWebProgress> webProgress;
  nsCOMPtr<nsIRequest> request;
  ReconstructWebProgressAndRequest(manager, aWebProgressData, aRequestData,
                                   getter_AddRefs(webProgress),
                                   getter_AddRefs(request));

  if (aWebProgressData && aWebProgressData->isTopLevel()) {
    Unused << browser->SetIsNavigating(aStateChangeData->isNavigating());
    Unused << browser->SetMayEnableCharacterEncodingMenu(
        aStateChangeData->mayEnableCharacterEncodingMenu());
    Unused << browser->UpdateForStateChange(aStateChangeData->charset(),
                                            aStateChangeData->documentURI(),
                                            aStateChangeData->contentType());
  } else if (aStateChangeData.isSome()) {
    return IPC_FAIL(
        this,
        "Unexpected WebProgressStateChangeData for non-top-level WebProgress");
  }

  Unused << managerAsListener->OnStateChange(webProgress, request, aStateFlags,
                                             aStatus);

  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvOnProgressChange(
    const Maybe<WebProgressData>& aWebProgressData,
    const RequestData& aRequestData, const int32_t aCurSelfProgress,
    const int32_t aMaxSelfProgress, const int32_t aCurTotalProgress,
    const int32_t aMaxTotalProgress) {
  nsCOMPtr<nsIBrowser> browser;
  nsCOMPtr<nsIWebProgress> manager;
  nsCOMPtr<nsIWebProgressListener> managerAsListener;
  if (!GetWebProgressListener(getter_AddRefs(browser), getter_AddRefs(manager),
                              getter_AddRefs(managerAsListener))) {
    return IPC_OK();
  }

  nsCOMPtr<nsIWebProgress> webProgress;
  nsCOMPtr<nsIRequest> request;
  ReconstructWebProgressAndRequest(manager, aWebProgressData, aRequestData,
                                   getter_AddRefs(webProgress),
                                   getter_AddRefs(request));

  Unused << managerAsListener->OnProgressChange(
      webProgress, request, aCurSelfProgress, aMaxSelfProgress,
      aCurTotalProgress, aMaxTotalProgress);

  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvOnLocationChange(
    const Maybe<WebProgressData>& aWebProgressData,
    const RequestData& aRequestData, nsIURI* aLocation, const uint32_t aFlags,
    const bool aCanGoBack, const bool aCanGoForward,
    const Maybe<WebProgressLocationChangeData>& aLocationChangeData) {
  nsCOMPtr<nsIBrowser> browser;
  nsCOMPtr<nsIWebProgress> manager;
  nsCOMPtr<nsIWebProgressListener> managerAsListener;
  if (!GetWebProgressListener(getter_AddRefs(browser), getter_AddRefs(manager),
                              getter_AddRefs(managerAsListener))) {
    return IPC_OK();
  }

  nsCOMPtr<nsIWebProgress> webProgress;
  nsCOMPtr<nsIRequest> request;
  ReconstructWebProgressAndRequest(manager, aWebProgressData, aRequestData,
                                   getter_AddRefs(webProgress),
                                   getter_AddRefs(request));

  Unused << browser->UpdateWebNavigationForLocationChange(aCanGoBack,
                                                          aCanGoForward);

  if (aWebProgressData && aWebProgressData->isTopLevel()) {
    nsCOMPtr<nsIPrincipal> contentBlockingAllowListPrincipal;
    Unused << browser->SetIsNavigating(aLocationChangeData->isNavigating());
    Unused << browser->UpdateForLocationChange(
        aLocation, aLocationChangeData->charset(),
        aLocationChangeData->mayEnableCharacterEncodingMenu(),
        aLocationChangeData->charsetAutodetected(),
        aLocationChangeData->documentURI(), aLocationChangeData->title(),
        aLocationChangeData->contentPrincipal(),
        aLocationChangeData->contentStoragePrincipal(),
        aLocationChangeData->contentBlockingAllowListPrincipal(),
        aLocationChangeData->csp(), aLocationChangeData->referrerInfo(),
        aLocationChangeData->isSyntheticDocument(),
        aWebProgressData->innerDOMWindowID(),
        aLocationChangeData->requestContextID().isSome(),
        aLocationChangeData->requestContextID().valueOr(0),
        aLocationChangeData->contentType());
  }

  Unused << managerAsListener->OnLocationChange(webProgress, request, aLocation,
                                                aFlags);

  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvOnStatusChange(
    const Maybe<WebProgressData>& aWebProgressData,
    const RequestData& aRequestData, const nsresult aStatus,
    const nsString& aMessage) {
  nsCOMPtr<nsIBrowser> browser;
  nsCOMPtr<nsIWebProgress> manager;
  nsCOMPtr<nsIWebProgressListener> managerAsListener;
  if (!GetWebProgressListener(getter_AddRefs(browser), getter_AddRefs(manager),
                              getter_AddRefs(managerAsListener))) {
    return IPC_OK();
  }

  nsCOMPtr<nsIWebProgress> webProgress;
  nsCOMPtr<nsIRequest> request;
  ReconstructWebProgressAndRequest(manager, aWebProgressData, aRequestData,
                                   getter_AddRefs(webProgress),
                                   getter_AddRefs(request));

  Unused << managerAsListener->OnStatusChange(webProgress, request, aStatus,
                                              aMessage.get());

  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvOnSecurityChange(
    const Maybe<WebProgressData>& aWebProgressData,
    const RequestData& aRequestData, const uint32_t aState,
    const Maybe<WebProgressSecurityChangeData>& aSecurityChangeData) {
  nsCOMPtr<nsIBrowser> browser;
  nsCOMPtr<nsIWebProgress> manager;
  nsCOMPtr<nsIWebProgressListener> managerAsListener;
  if (!GetWebProgressListener(getter_AddRefs(browser), getter_AddRefs(manager),
                              getter_AddRefs(managerAsListener))) {
    return IPC_OK();
  }

  nsCOMPtr<nsIWebProgress> webProgress;
  nsCOMPtr<nsIRequest> request;
  ReconstructWebProgressAndRequest(manager, aWebProgressData, aRequestData,
                                   getter_AddRefs(webProgress),
                                   getter_AddRefs(request));

  if (aWebProgressData && aWebProgressData->isTopLevel()) {
    Unused << browser->UpdateSecurityUIForSecurityChange(
        aSecurityChangeData->securityInfo(), aState,
        aSecurityChangeData->isSecureContext());
  }

  Unused << managerAsListener->OnSecurityChange(webProgress, request, aState);

  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvOnContentBlockingEvent(
    const Maybe<WebProgressData>& aWebProgressData,
    const RequestData& aRequestData, const uint32_t& aEvent) {
  nsCOMPtr<nsIBrowser> browser;
  nsCOMPtr<nsIWebProgress> manager;
  nsCOMPtr<nsIWebProgressListener> managerAsListener;
  if (!GetWebProgressListener(getter_AddRefs(browser), getter_AddRefs(manager),
                              getter_AddRefs(managerAsListener))) {
    return IPC_OK();
  }

  nsCOMPtr<nsIWebProgress> webProgress;
  nsCOMPtr<nsIRequest> request;
  ReconstructWebProgressAndRequest(manager, aWebProgressData, aRequestData,
                                   getter_AddRefs(webProgress),
                                   getter_AddRefs(request));

  Unused << managerAsListener->OnContentBlockingEvent(webProgress, request,
                                                      aEvent);

  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvNavigationFinished() {
  nsCOMPtr<nsIBrowser> browser =
      mFrameElement ? mFrameElement->AsBrowser() : nullptr;

  if (browser) {
    browser->SetIsNavigating(false);
  }

  return IPC_OK();
}

bool BrowserParent::GetWebProgressListener(
    nsIBrowser** aOutBrowser, nsIWebProgress** aOutManager,
    nsIWebProgressListener** aOutListener) {
  MOZ_ASSERT(aOutBrowser);
  MOZ_ASSERT(aOutManager);
  MOZ_ASSERT(aOutListener);

  nsCOMPtr<nsIBrowser> browser;
  RefPtr<Element> currentElement = mFrameElement;

  // In Responsive Design Mode, mFrameElement will be the <iframe mozbrowser>,
  // but we want the <xul:browser> that it is embedded in.
  while (currentElement) {
    browser = currentElement->AsBrowser();
    if (browser) {
      break;
    }

    BrowsingContext* browsingContext =
        currentElement->OwnerDoc()->GetBrowsingContext();
    currentElement =
        browsingContext ? browsingContext->GetEmbedderElement() : nullptr;
  }

  if (!browser) {
    return false;
  }

  nsCOMPtr<nsIWebProgress> manager;
  nsresult rv = browser->GetRemoteWebProgressManager(getter_AddRefs(manager));
  if (NS_FAILED(rv)) {
    return false;
  }

  nsCOMPtr<nsIWebProgressListener> listener = do_QueryInterface(manager);
  if (!listener) {
    // We are no longer remote so we cannot forward this event.
    return false;
  }

  browser.forget(aOutBrowser);
  manager.forget(aOutManager);
  listener.forget(aOutListener);

  return true;
}

void BrowserParent::ReconstructWebProgressAndRequest(
    nsIWebProgress* aManager, const Maybe<WebProgressData>& aWebProgressData,
    const RequestData& aRequestData, nsIWebProgress** aOutWebProgress,
    nsIRequest** aOutRequest) {
  MOZ_DIAGNOSTIC_ASSERT(aOutWebProgress,
                        "aOutWebProgress should never be null");
  MOZ_DIAGNOSTIC_ASSERT(aOutRequest, "aOutRequest should never be null");

  nsCOMPtr<nsIWebProgress> webProgress;
  if (aWebProgressData) {
    webProgress = new RemoteWebProgress(
        aManager, aWebProgressData->outerDOMWindowID(),
        aWebProgressData->innerDOMWindowID(), aWebProgressData->loadType(),
        aWebProgressData->isLoadingDocument(), aWebProgressData->isTopLevel());
  } else {
    webProgress = new RemoteWebProgress(aManager, 0, 0, 0, false, false);
  }
  webProgress.forget(aOutWebProgress);

  if (aRequestData.requestURI()) {
    nsCOMPtr<nsIRequest> request = MakeAndAddRef<RemoteWebProgressRequest>(
        aRequestData.requestURI(), aRequestData.originalRequestURI(),
        aRequestData.matchedList(), aRequestData.elapsedLoadTimeMS());
    request.forget(aOutRequest);
  } else {
    *aOutRequest = nullptr;
  }
}

mozilla::ipc::IPCResult BrowserParent::RecvSessionStoreUpdate(
    const Maybe<nsCString>& aDocShellCaps, const Maybe<bool>& aPrivatedMode,
    nsTArray<nsCString>&& aPositions,
    nsTArray<int32_t>&& aPositionDescendants,
    const nsTArray<InputFormData>& aInputs,
    const nsTArray<CollectedInputDataValue>& aIdVals,
    const nsTArray<CollectedInputDataValue>& aXPathVals,
    nsTArray<nsCString>&& aOrigins, nsTArray<nsString>&& aKeys,
    nsTArray<nsString>&& aValues, const bool aIsFullStorage,
    const uint32_t& aFlushId, const bool& aIsFinal, const uint32_t& aEpoch) {
  UpdateSessionStoreData data;
  if (aDocShellCaps.isSome()) {
    data.mDocShellCaps.Construct() = aDocShellCaps.value();
  }
  if (aPrivatedMode.isSome()) {
    data.mIsPrivate.Construct() = aPrivatedMode.value();
  }
  if (aPositions.Length() != 0) {
    data.mPositions.Construct().Assign(std::move(aPositions));
    data.mPositionDescendants.Construct().Assign(
        std::move(aPositionDescendants));
  }
  if (aIdVals.Length() != 0) {
    SessionStoreUtils::ComposeInputData(aIdVals, data.mId.Construct());
  }
  if (aXPathVals.Length() != 0) {
    SessionStoreUtils::ComposeInputData(aXPathVals, data.mXpath.Construct());
  }
  if (aInputs.Length() != 0) {
    nsTArray<int> descendants, numId, numXPath;
    nsTArray<nsString> innerHTML;
    nsTArray<nsCString> url;
    for (const InputFormData& input : aInputs) {
      descendants.AppendElement(input.descendants);
      numId.AppendElement(input.numId);
      numXPath.AppendElement(input.numXPath);
      innerHTML.AppendElement(input.innerHTML);
      url.AppendElement(input.url);
    }

    data.mInputDescendants.Construct().Assign(std::move(descendants));
    data.mNumId.Construct().Assign(std::move(numId));
    data.mNumXPath.Construct().Assign(std::move(numXPath));
    data.mInnerHTML.Construct().Assign(std::move(innerHTML));
    data.mUrl.Construct().Assign(std::move(url));
  }
  // In normal case, we only update the storage when needed.
  // However, we need to reset the session storage(aOrigins.Length() will be 0)
  //   if the usage is over the "browser_sessionstore_dom_storage_limit".
  // In this case, aIsFullStorage is true.
  if (aOrigins.Length() != 0 || aIsFullStorage) {
    data.mStorageOrigins.Construct().Assign(std::move(aOrigins));
    data.mStorageKeys.Construct().Assign(std::move(aKeys));
    data.mStorageValues.Construct().Assign(std::move(aValues));
    data.mIsFullStorage.Construct() = aIsFullStorage;
  }

  nsCOMPtr<nsISessionStoreFunctions> funcs =
      do_ImportModule("resource://gre/modules/SessionStoreFunctions.jsm");
  NS_ENSURE_TRUE(funcs, IPC_OK());
  nsCOMPtr<nsIXPConnectWrappedJS> wrapped = do_QueryInterface(funcs);
  AutoJSAPI jsapi;
  MOZ_ALWAYS_TRUE(jsapi.Init(wrapped->GetJSObjectGlobal()));
  JS::Rooted<JS::Value> dataVal(jsapi.cx());
  bool ok = ToJSValue(jsapi.cx(), data, &dataVal);
  NS_ENSURE_TRUE(ok, IPC_OK());

  nsresult rv = funcs->UpdateSessionStore(mFrameElement, aFlushId, aIsFinal,
                                          aEpoch, dataVal);
  NS_ENSURE_SUCCESS(rv, IPC_OK());

  return IPC_OK();
}

bool BrowserParent::HandleQueryContentEvent(WidgetQueryContentEvent& aEvent) {
  nsCOMPtr<nsIWidget> textInputHandlingWidget = GetTextInputHandlingWidget();
  if (!textInputHandlingWidget) {
    return true;
  }
  if (NS_WARN_IF(!mContentCache.HandleQueryContentEvent(
          aEvent, textInputHandlingWidget)) ||
      NS_WARN_IF(!aEvent.mSucceeded)) {
    return true;
  }
  switch (aEvent.mMessage) {
    case eQueryTextRect:
    case eQueryCaretRect:
    case eQueryEditorRect: {
      nsCOMPtr<nsIWidget> browserWidget = GetWidget();
      if (browserWidget != textInputHandlingWidget) {
        aEvent.mReply.mRect += nsLayoutUtils::WidgetToWidgetOffset(
            browserWidget, textInputHandlingWidget);
      }
      aEvent.mReply.mRect = TransformChildToParent(aEvent.mReply.mRect);
      break;
    }
    default:
      break;
  }
  return true;
}

bool BrowserParent::SendCompositionEvent(WidgetCompositionEvent& aEvent) {
  if (mIsDestroyed) {
    return false;
  }

  if (!mContentCache.OnCompositionEvent(aEvent)) {
    return true;
  }

  bool ret = Manager()->IsInputPriorityEventEnabled()
                 ? PBrowserParent::SendCompositionEvent(aEvent)
                 : PBrowserParent::SendNormalPriorityCompositionEvent(aEvent);
  if (NS_WARN_IF(!ret)) {
    return false;
  }
  MOZ_ASSERT(aEvent.HasBeenPostedToRemoteProcess());
  return true;
}

bool BrowserParent::SendSelectionEvent(WidgetSelectionEvent& aEvent) {
  if (mIsDestroyed) {
    return false;
  }
  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (!widget) {
    return true;
  }
  mContentCache.OnSelectionEvent(aEvent);
  bool ret = Manager()->IsInputPriorityEventEnabled()
                 ? PBrowserParent::SendSelectionEvent(aEvent)
                 : PBrowserParent::SendNormalPrioritySelectionEvent(aEvent);
  if (NS_WARN_IF(!ret)) {
    return false;
  }
  MOZ_ASSERT(aEvent.HasBeenPostedToRemoteProcess());
  aEvent.mSucceeded = true;
  return true;
}

bool BrowserParent::SendPasteTransferable(const IPCDataTransfer& aDataTransfer,
                                          const bool& aIsPrivateData,
                                          nsIPrincipal* aRequestingPrincipal,
                                          const uint32_t& aContentPolicyType) {
  return PBrowserParent::SendPasteTransferable(
      aDataTransfer, aIsPrivateData, aRequestingPrincipal, aContentPolicyType);
}

/* static */
void BrowserParent::PushFocus(BrowserParent* aBrowserParent) {
  if (!sFocusStack) {
    MOZ_ASSERT_UNREACHABLE("PushFocus when not initialized");
    return;
  }
  if (!aBrowserParent->GetBrowserBridgeParent()) {
    // top-level Web content
    // When a new native window is created, we spin a nested event loop.
    // As a result, unlike when raising an existing window, we get
    // PushFocus for content in the new window before we get the PopFocus
    // for content in the old one. Hence, if the stack isn't empty when
    // pushing top-level Web content, first pop everything off the stack.
    PopFocusAll();
    MOZ_ASSERT(sFocusStack->IsEmpty());
  } else {
    // out-of-process iframe
    // Considering that we can get top-level pushes out of order, let's
    // ignore trailing out-of-process iframe pushes for the previous top-level
    // Web content.
    if (sFocusStack->IsEmpty()) {
      LOGBROWSERFOCUS(
          ("PushFocus for out-of-process iframe ignored with empty stack %p",
           aBrowserParent));
      return;
    }
    nsCOMPtr<nsIWidget> webRootWidget = sFocusStack->ElementAt(0)->GetWidget();
    nsCOMPtr<nsIWidget> iframeWigdet = aBrowserParent->GetWidget();
    if (webRootWidget != iframeWigdet) {
      LOGBROWSERFOCUS(
          ("PushFocus for out-of-process iframe ignored with mismatching "
           "top-level content %p",
           aBrowserParent));
      return;
    }
  }
  if (sFocusStack->Contains(aBrowserParent)) {
    MOZ_ASSERT_UNREACHABLE(
        "Trying to push a BrowserParent that is already on the stack");
    return;
  }
  BrowserParent* old = GetFocused();
  sFocusStack->AppendElement(aBrowserParent);
  MOZ_ASSERT(GetFocused() == aBrowserParent);
  LOGBROWSERFOCUS(("PushFocus changed focus to %p", aBrowserParent));
  IMEStateManager::OnFocusMovedBetweenBrowsers(old, aBrowserParent);
}

/* static */
void BrowserParent::PopFocus(BrowserParent* aBrowserParent) {
  if (!sFocusStack) {
    MOZ_ASSERT_UNREACHABLE("PopFocus when not initialized");
    return;
  }
  // When focus is in an out-of-process iframe and the whole window
  // or tab loses focus, we first receive a pop for the top-level Web
  // content process and only then for its out-of-process iframes.
  // Hence, we do all the popping up front and then ignore the
  // pop requests for the out-of-process iframes that we already
  // popped.
  auto pos = sFocusStack->LastIndexOf(aBrowserParent);
  if (pos == nsTArray<BrowserParent*>::NoIndex) {
    LOGBROWSERFOCUS(("PopFocus not on stack %p", aBrowserParent));
    return;
  }
  auto len = sFocusStack->Length();
  auto itemsToPop = len - pos;
  LOGBROWSERFOCUS(("PopFocus pops %zu items %p", itemsToPop, aBrowserParent));
  while (pos < sFocusStack->Length()) {
    BrowserParent* popped = sFocusStack->PopLastElement();
    BrowserParent* focused = GetFocused();
    LOGBROWSERFOCUS(("PopFocus changed focus to %p", focused));
    IMEStateManager::OnFocusMovedBetweenBrowsers(popped, focused);
  }
}

/* static */
void BrowserParent::PopFocusAll() {
  if (!sFocusStack->IsEmpty()) {
    LOGBROWSERFOCUS(("PopFocusAll pops items"));
    PopFocus(sFocusStack->ElementAt(0));
  } else {
    LOGBROWSERFOCUS(("PopFocusAll does nothing"));
  }
}

mozilla::ipc::IPCResult BrowserParent::RecvRequestIMEToCommitComposition(
    const bool& aCancel, bool* aIsCommitted, nsString* aCommittedString) {
  nsCOMPtr<nsIWidget> widget = GetTextInputHandlingWidget();
  if (!widget) {
    *aIsCommitted = false;
    return IPC_OK();
  }

  *aIsCommitted = mContentCache.RequestIMEToCommitComposition(
      widget, aCancel, *aCommittedString);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvStartPluginIME(
    const WidgetKeyboardEvent& aKeyboardEvent, const int32_t& aPanelX,
    const int32_t& aPanelY, nsString* aCommitted) {
  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (!widget) {
    return IPC_OK();
  }
  Unused << widget->StartPluginIME(aKeyboardEvent, (int32_t&)aPanelX,
                                   (int32_t&)aPanelY, *aCommitted);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvSetPluginFocused(
    const bool& aFocused) {
  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (!widget) {
    return IPC_OK();
  }
  widget->SetPluginFocused((bool&)aFocused);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvSetCandidateWindowForPlugin(
    const CandidateWindowPosition& aPosition) {
  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (!widget) {
    return IPC_OK();
  }

  widget->SetCandidateWindowForPlugin(aPosition);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvEnableIMEForPlugin(
    const bool& aEnable) {
  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (!widget) {
    return IPC_OK();
  }
  widget->EnableIMEForPlugin(aEnable);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvDefaultProcOfPluginEvent(
    const WidgetPluginEvent& aEvent) {
  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (!widget) {
    return IPC_OK();
  }

  widget->DefaultProcOfPluginEvent(aEvent);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvGetInputContext(
    widget::IMEState* aState) {
  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (!widget) {
    *aState = widget::IMEState(IMEState::DISABLED,
                               IMEState::OPEN_STATE_NOT_SUPPORTED);
    return IPC_OK();
  }

  *aState = widget->GetInputContext().mIMEState;
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvSetInputContext(
    const InputContext& aContext, const InputContextAction& aAction) {
  IMEStateManager::SetInputContextForChildProcess(this, aContext, aAction);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvSetNativeChildOfShareableWindow(
    const uintptr_t& aChildWindow) {
#if defined(XP_WIN)
  nsCOMPtr<nsIWidget> widget = GetTopLevelWidget();
  if (widget) {
    // Note that this call will probably cause a sync native message to the
    // process that owns the child window.
    widget->SetNativeData(NS_NATIVE_CHILD_OF_SHAREABLE_WINDOW, aChildWindow);
  }
  return IPC_OK();
#else
  MOZ_ASSERT_UNREACHABLE(
      "BrowserParent::RecvSetNativeChildOfShareableWindow not implemented!");
  return IPC_FAIL_NO_REASON(this);
#endif
}

mozilla::ipc::IPCResult BrowserParent::RecvDispatchFocusToTopLevelWindow() {
  if (nsCOMPtr<nsIWidget> widget = GetTopLevelWidget()) {
    widget->SetFocus(nsIWidget::Raise::No);
  }
  return IPC_OK();
}

bool BrowserParent::ReceiveMessage(const nsString& aMessage, bool aSync,
                                   StructuredCloneData* aData,
                                   nsTArray<StructuredCloneData>* aRetVal) {
  // If we're for an oop iframe, don't deliver messages to the wrong place.
  if (mBrowserBridgeParent) {
    return true;
  }

  RefPtr<nsFrameLoader> frameLoader = GetFrameLoader(true);
  if (frameLoader && frameLoader->GetFrameMessageManager()) {
    RefPtr<nsFrameMessageManager> manager =
        frameLoader->GetFrameMessageManager();

    manager->ReceiveMessage(mFrameElement, frameLoader, aMessage, aSync, aData,
                            aRetVal, IgnoreErrors());
  }
  return true;
}

// nsIAuthPromptProvider

// This method is largely copied from nsDocShell::GetAuthPrompt
NS_IMETHODIMP
BrowserParent::GetAuthPrompt(uint32_t aPromptReason, const nsIID& iid,
                             void** aResult) {
  // we're either allowing auth, or it's a proxy request
  nsresult rv;
  nsCOMPtr<nsIPromptFactory> wwatch =
      do_GetService(NS_WINDOWWATCHER_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsPIDOMWindowOuter> window;
  RefPtr<Element> frame = mFrameElement;
  if (frame) window = frame->OwnerDoc()->GetWindow();

  // Get an auth prompter for our window so that the parenting
  // of the dialogs works as it should when using tabs.
  nsCOMPtr<nsISupports> prompt;
  rv = wwatch->GetPrompt(window, iid, getter_AddRefs(prompt));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsILoginManagerPrompter> prompter = do_QueryInterface(prompt);
  if (prompter) {
    prompter->SetBrowser(mFrameElement);
  }

  *aResult = prompt.forget().take();
  return NS_OK;
}

PColorPickerParent* BrowserParent::AllocPColorPickerParent(
    const nsString& aTitle, const nsString& aInitialColor) {
  return new ColorPickerParent(aTitle, aInitialColor);
}

bool BrowserParent::DeallocPColorPickerParent(PColorPickerParent* actor) {
  delete actor;
  return true;
}

already_AddRefed<nsFrameLoader> BrowserParent::GetFrameLoader(
    bool aUseCachedFrameLoaderAfterDestroy) const {
  if (mIsDestroyed && !aUseCachedFrameLoaderAfterDestroy) {
    return nullptr;
  }

  if (mFrameLoader) {
    RefPtr<nsFrameLoader> fl = mFrameLoader;
    return fl.forget();
  }
  RefPtr<Element> frameElement(mFrameElement);
  RefPtr<nsFrameLoaderOwner> frameLoaderOwner = do_QueryObject(frameElement);
  return frameLoaderOwner ? frameLoaderOwner->GetFrameLoader() : nullptr;
}

void BrowserParent::TryCacheDPIAndScale() {
  if (mDPI > 0) {
    return;
  }

  nsCOMPtr<nsIWidget> widget = GetWidget();

  if (widget) {
    mDPI = widget->GetDPI();
    mRounding = widget->RoundsWidgetCoordinatesTo();
    mDefaultScale = widget->GetDefaultScale();
  }
}

void BrowserParent::ApzAwareEventRoutingToChild(
    ScrollableLayerGuid* aOutTargetGuid, uint64_t* aOutInputBlockId,
    nsEventStatus* aOutApzResponse) {
  // Let the widget know that the event will be sent to the child process,
  // which will (hopefully) send a confirmation notice back to APZ.
  // Do this even if APZ is off since we need it for swipe gesture support on
  // OS X without APZ.
  InputAPZContext::SetRoutedToChildProcess();

  if (AsyncPanZoomEnabled()) {
    if (aOutTargetGuid) {
      *aOutTargetGuid = InputAPZContext::GetTargetLayerGuid();

      // There may be cases where the APZ hit-testing code came to a different
      // conclusion than the main-thread hit-testing code as to where the event
      // is destined. In such cases the layersId of the APZ result may not match
      // the layersId of this RemoteLayerTreeOwner. In such cases the
      // main-thread hit- testing code "wins" so we need to update the guid to
      // reflect this.
      if (mRemoteLayerTreeOwner.IsInitialized()) {
        if (aOutTargetGuid->mLayersId != mRemoteLayerTreeOwner.GetLayersId()) {
          *aOutTargetGuid =
              ScrollableLayerGuid(mRemoteLayerTreeOwner.GetLayersId(), 0,
                                  ScrollableLayerGuid::NULL_SCROLL_ID);
        }
      }
    }
    if (aOutInputBlockId) {
      *aOutInputBlockId = InputAPZContext::GetInputBlockId();
    }
    if (aOutApzResponse) {
      *aOutApzResponse = InputAPZContext::GetApzResponse();
    }
  } else {
    if (aOutInputBlockId) {
      *aOutInputBlockId = 0;
    }
    if (aOutApzResponse) {
      *aOutApzResponse = nsEventStatus_eIgnore;
    }
  }
}

mozilla::ipc::IPCResult BrowserParent::RecvBrowserFrameOpenWindow(
    PBrowserParent* aOpener, const nsString& aURL, const nsString& aName,
    bool aForceNoReferrer, const nsString& aFeatures,
    BrowserFrameOpenWindowResolver&& aResolve) {
  CreatedWindowInfo cwi;
  cwi.rv() = NS_OK;
  cwi.maxTouchPoints() = 0;

  BrowserElementParent::OpenWindowResult opened =
      BrowserElementParent::OpenWindowOOP(BrowserParent::GetFrom(aOpener), this,
                                          aURL, aName, aForceNoReferrer,
                                          aFeatures);
  cwi.windowOpened() = (opened == BrowserElementParent::OPEN_WINDOW_ADDED);
  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (widget) {
    cwi.maxTouchPoints() = widget->GetMaxTouchPoints();
    cwi.dimensions() = GetDimensionInfo();
  }

  // Resolve the request with the information we collected.
  aResolve(cwi);

  if (!cwi.windowOpened()) {
    Destroy();
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvRespondStartSwipeEvent(
    const uint64_t& aInputBlockId, const bool& aStartSwipe) {
  if (nsCOMPtr<nsIWidget> widget = GetWidget()) {
    widget->ReportSwipeStarted(aInputBlockId, aStartSwipe);
  }
  return IPC_OK();
}

// defined in nsIRemoteTab
NS_IMETHODIMP
BrowserParent::SetDocShellIsActive(bool isActive) {
  mDocShellIsActive = isActive;
  SetRenderLayers(isActive);
  Unused << SendSetDocShellIsActive(isActive);

  // update active accessible documents on windows
#if defined(XP_WIN) && defined(ACCESSIBILITY)
  if (a11y::Compatibility::IsDolphin()) {
    if (a11y::DocAccessibleParent* tabDoc = GetTopLevelDocAccessible()) {
      HWND window = tabDoc->GetEmulatedWindowHandle();
      MOZ_ASSERT(window);
      if (window) {
        if (isActive) {
          a11y::nsWinUtils::ShowNativeWindow(window);
        } else {
          a11y::nsWinUtils::HideNativeWindow(window);
        }
      }
    }
  }
#endif
  return NS_OK;
}

NS_IMETHODIMP
BrowserParent::GetDocShellIsActive(bool* aIsActive) {
  *aIsActive = mDocShellIsActive;
  return NS_OK;
}

NS_IMETHODIMP
BrowserParent::SetRenderLayers(bool aEnabled) {
  if (mActiveInPriorityManager != aEnabled) {
    mActiveInPriorityManager = aEnabled;
    // Let's inform the priority manager. This operation can end up with the
    // changing of the process priority.
    ProcessPriorityManager::TabActivityChanged(this, aEnabled);
  }

  if (aEnabled == mRenderLayers) {
    if (aEnabled && mHasLayers && mPreserveLayers) {
      // RenderLayers might be called when we've been preserving layers,
      // and already had layers uploaded. In that case, the MozLayerTreeReady
      // event will not naturally arrive, which can confuse the front-end
      // layer. So we fire the event here.
      RefPtr<BrowserParent> self = this;
      LayersObserverEpoch epoch = mLayerTreeEpoch;
      NS_DispatchToMainThread(NS_NewRunnableFunction(
          "dom::BrowserParent::RenderLayers", [self, epoch]() {
            MOZ_ASSERT(NS_IsMainThread());
            self->LayerTreeUpdate(epoch, true);
          }));
    }

    return NS_OK;
  }

  // Preserve layers means that attempts to stop rendering layers
  // will be ignored.
  if (!aEnabled && mPreserveLayers) {
    return NS_OK;
  }

  mRenderLayers = aEnabled;

  SetRenderLayersInternal(aEnabled, false /* aForceRepaint */);
  return NS_OK;
}

NS_IMETHODIMP
BrowserParent::GetRenderLayers(bool* aResult) {
  *aResult = mRenderLayers;
  return NS_OK;
}

NS_IMETHODIMP
BrowserParent::GetHasLayers(bool* aResult) {
  *aResult = mHasLayers;
  return NS_OK;
}

NS_IMETHODIMP
BrowserParent::Deprioritize() {
  if (mActiveInPriorityManager) {
    ProcessPriorityManager::TabActivityChanged(this, false);
    mActiveInPriorityManager = false;
  }
  return NS_OK;
}

NS_IMETHODIMP
BrowserParent::ForceRepaint() {
  if (!mActiveInPriorityManager) {
    // If a tab is left and then returned to very rapidly, it can be
    // deprioritized without losing its loaded status. In this case we won't
    // go through SetRenderLayers.
    mActiveInPriorityManager = true;
    ProcessPriorityManager::TabActivityChanged(this, true);
  }

  SetRenderLayersInternal(true /* aEnabled */, true /* aForceRepaint */);
  return NS_OK;
}

void BrowserParent::SetRenderLayersInternal(bool aEnabled, bool aForceRepaint) {
  // Increment the epoch so that layer tree updates from previous
  // RenderLayers requests are ignored.
  mLayerTreeEpoch = mLayerTreeEpoch.Next();

  Unused << SendRenderLayers(aEnabled, aForceRepaint, mLayerTreeEpoch);

  // Ask the child to repaint using the PHangMonitor channel/thread (which may
  // be less congested).
  if (aEnabled) {
    Manager()->PaintTabWhileInterruptingJS(this, aForceRepaint,
                                           mLayerTreeEpoch);
  }
}

NS_IMETHODIMP
BrowserParent::PreserveLayers(bool aPreserveLayers) {
  mPreserveLayers = aPreserveLayers;
  return NS_OK;
}

NS_IMETHODIMP
BrowserParent::GetContentBlockingLog(Promise** aPromise) {
  NS_ENSURE_ARG_POINTER(aPromise);

  *aPromise = nullptr;
  if (!mFrameElement) {
    return NS_ERROR_FAILURE;
  }

  ErrorResult rv;
  RefPtr<Promise> jsPromise =
      Promise::Create(mFrameElement->OwnerDoc()->GetOwnerGlobal(), rv);
  if (rv.Failed()) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<Promise> copy(jsPromise);
  copy.forget(aPromise);

  auto cblPromise = SendGetContentBlockingLog();
  cblPromise->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [jsPromise](Tuple<nsCString, bool>&& aResult) {
        if (Get<1>(aResult)) {
          NS_ConvertUTF8toUTF16 utf16(Get<0>(aResult));
          jsPromise->MaybeResolve(std::move(utf16));
        } else {
          jsPromise->MaybeRejectWithUndefined();
        }
      },
      [jsPromise](ResponseRejectReason&& aReason) {
        jsPromise->MaybeRejectWithUndefined();
      });

  return NS_OK;
}

NS_IMETHODIMP
BrowserParent::MaybeCancelContentJSExecutionFromScript(
    nsIRemoteTab::NavigationType aNavigationType,
    JS::Handle<JS::Value> aCancelContentJSOptions, JSContext* aCx) {
  dom::CancelContentJSOptions cancelContentJSOptions;
  if (!cancelContentJSOptions.Init(aCx, aCancelContentJSOptions)) {
    return NS_ERROR_INVALID_ARG;
  }
  if (StaticPrefs::dom_ipc_cancel_content_js_when_navigating()) {
    Manager()->CancelContentJSExecutionIfRunning(this, aNavigationType,
                                                 cancelContentJSOptions);
  }
  return NS_OK;
}

void BrowserParent::SuppressDisplayport(bool aEnabled) {
  if (IsDestroyed()) {
    return;
  }

#ifdef DEBUG
  if (aEnabled) {
    mActiveSupressDisplayportCount++;
  } else {
    mActiveSupressDisplayportCount--;
  }
  MOZ_ASSERT(mActiveSupressDisplayportCount >= 0);
#endif

  Unused << SendSuppressDisplayport(aEnabled);
}

NS_IMETHODIMP
BrowserParent::GetTabId(uint64_t* aId) {
  *aId = GetTabId();
  return NS_OK;
}

NS_IMETHODIMP
BrowserParent::GetOsPid(int32_t* aId) {
  *aId = Manager()->Pid();
  return NS_OK;
}

NS_IMETHODIMP
BrowserParent::GetHasContentOpener(bool* aResult) {
  *aResult = mHasContentOpener;
  return NS_OK;
}

void BrowserParent::SetHasContentOpener(bool aHasContentOpener) {
  mHasContentOpener = aHasContentOpener;
}

NS_IMETHODIMP
BrowserParent::GetHasPresented(bool* aResult) {
  *aResult = mHasPresented;
  return NS_OK;
}

void BrowserParent::NavigateByKey(bool aForward, bool aForDocumentNavigation) {
  Unused << SendNavigateByKey(aForward, aForDocumentNavigation);
}

NS_IMETHODIMP
BrowserParent::GetWindowGlobalParents(
    nsTArray<RefPtr<WindowGlobalParent>>& aWindowGlobalParents) {
  VisitAll([&aWindowGlobalParents](BrowserParent* aBrowser) {
    const auto& windowGlobalParents = aBrowser->ManagedPWindowGlobalParent();
    for (auto iter = windowGlobalParents.ConstIter(); !iter.Done();
         iter.Next()) {
      WindowGlobalParent* windowGlobalParent =
          static_cast<WindowGlobalParent*>(iter.Get()->GetKey());
      aWindowGlobalParents.AppendElement(windowGlobalParent);
    }
  });
  return NS_OK;
}

NS_IMETHODIMP
BrowserParent::TransmitPermissionsForPrincipal(nsIPrincipal* aPrincipal) {
  return Manager()->TransmitPermissionsForPrincipal(aPrincipal);
}

NS_IMETHODIMP
BrowserParent::GetHasBeforeUnload(bool* aResult) {
  *aResult = mHasBeforeUnload;
  return NS_OK;
}

void BrowserParent::LayerTreeUpdate(const LayersObserverEpoch& aEpoch,
                                    bool aActive) {
  // Ignore updates from old epochs. They might tell us that layers are
  // available when we've already sent a message to clear them. We can't trust
  // the update in that case since layers could disappear anytime after that.
  if (aEpoch != mLayerTreeEpoch || mIsDestroyed) {
    return;
  }

  RefPtr<EventTarget> target = mFrameElement;
  if (!target) {
    NS_WARNING("Could not locate target for layer tree message.");
    return;
  }

  mHasLayers = aActive;

  RefPtr<Event> event = NS_NewDOMEvent(mFrameElement, nullptr, nullptr);
  if (aActive) {
    mHasPresented = true;
    event->InitEvent(u"MozLayerTreeReady"_ns, true, false);
  } else {
    event->InitEvent(u"MozLayerTreeCleared"_ns, true, false);
  }
  event->SetTrusted(true);
  event->WidgetEventPtr()->mFlags.mOnlyChromeDispatch = true;
  mFrameElement->DispatchEvent(*event);
}

void BrowserParent::RequestRootPaint(gfx::CrossProcessPaint* aPaint,
                                     IntRect aRect, float aScale,
                                     nscolor aBackgroundColor) {
  auto promise = SendRequestRootPaint(aRect, aScale, aBackgroundColor);

  RefPtr<gfx::CrossProcessPaint> paint(aPaint);
  TabId tabId(GetTabId());
  promise->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [paint, tabId](PaintFragment&& aFragment) {
        paint->ReceiveFragment(tabId, std::move(aFragment));
      },
      [paint, tabId](ResponseRejectReason&& aReason) {
        paint->LostFragment(tabId);
      });
}

void BrowserParent::RequestSubPaint(gfx::CrossProcessPaint* aPaint,
                                    float aScale, nscolor aBackgroundColor) {
  auto promise = SendRequestSubPaint(aScale, aBackgroundColor);

  RefPtr<gfx::CrossProcessPaint> paint(aPaint);
  TabId tabId(GetTabId());
  promise->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [paint, tabId](PaintFragment&& aFragment) {
        paint->ReceiveFragment(tabId, std::move(aFragment));
      },
      [paint, tabId](ResponseRejectReason&& aReason) {
        paint->LostFragment(tabId);
      });
}

mozilla::ipc::IPCResult BrowserParent::RecvPaintWhileInterruptingJSNoOp(
    const LayersObserverEpoch& aEpoch) {
  // We sent a PaintWhileInterruptingJS message when layers were already
  // visible. In this case, we should act as if an update occurred even though
  // we already have the layers.
  LayerTreeUpdate(aEpoch, true);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvRemotePaintIsReady() {
  RefPtr<EventTarget> target = mFrameElement;
  if (!target) {
    NS_WARNING("Could not locate target for MozAfterRemotePaint message.");
    return IPC_OK();
  }

  RefPtr<Event> event = NS_NewDOMEvent(mFrameElement, nullptr, nullptr);
  event->InitEvent(u"MozAfterRemotePaint"_ns, false, false);
  event->SetTrusted(true);
  event->WidgetEventPtr()->mFlags.mOnlyChromeDispatch = true;
  mFrameElement->DispatchEvent(*event);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvNotifyCompositorTransaction() {
  RefPtr<nsFrameLoader> frameLoader = GetFrameLoader();

  if (!frameLoader) {
    return IPC_OK();
  }

  nsIFrame* docFrame = frameLoader->GetPrimaryFrameOfOwningContent();

  if (!docFrame) {
    // Bad, but nothing we can do about it (XXX/cjones: or is there?
    // maybe bug 589337?).  When the new frame is created, we'll
    // probably still be the current render frame and will get to draw
    // our content then.  Or, we're shutting down and this update goes
    // to /dev/null.
    return IPC_OK();
  }

  docFrame->InvalidateLayer(DisplayItemType::TYPE_REMOTE);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvRemoteIsReadyToHandleInputEvents() {
  // When enabling input event prioritization, input events may preempt other
  // normal priority IPC messages. To prevent the input events preempt
  // PBrowserConstructor, we use an IPC 'RemoteIsReadyToHandleInputEvents' to
  // notify the parent that BrowserChild is created and ready to handle input
  // events.
  SetReadyToHandleInputEvents();
  return IPC_OK();
}

mozilla::plugins::PPluginWidgetParent*
BrowserParent::AllocPPluginWidgetParent() {
#ifdef XP_WIN
  return new mozilla::plugins::PluginWidgetParent();
#else
  MOZ_ASSERT_UNREACHABLE("AllocPPluginWidgetParent only supports Windows");
  return nullptr;
#endif
}

bool BrowserParent::DeallocPPluginWidgetParent(
    mozilla::plugins::PPluginWidgetParent* aActor) {
  delete aActor;
  return true;
}

nsresult BrowserParent::HandleEvent(Event* aEvent) {
  if (mIsDestroyed) {
    return NS_OK;
  }

  nsAutoString eventType;
  aEvent->GetType(eventType);
  if (eventType.EqualsLiteral("MozUpdateWindowPos") ||
      eventType.EqualsLiteral("fullscreenchange")) {
    // Events that signify the window moving are used to update the position
    // and notify the BrowserChild.
    return UpdatePosition();
  }
  return NS_OK;
}

class FakeChannel final : public nsIChannel,
                          public nsIAuthPromptCallback,
                          public nsIInterfaceRequestor,
                          public nsILoadContext {
 public:
  FakeChannel(const nsCString& aUri, uint64_t aCallbackId, Element* aElement)
      : mCallbackId(aCallbackId), mElement(aElement) {
    NS_NewURI(getter_AddRefs(mUri), aUri);
  }

  NS_DECL_ISUPPORTS

#define NO_IMPL \
  override { return NS_ERROR_NOT_IMPLEMENTED; }
  NS_IMETHOD GetName(nsACString&) NO_IMPL;
  NS_IMETHOD IsPending(bool*) NO_IMPL;
  NS_IMETHOD GetStatus(nsresult*) NO_IMPL;
  NS_IMETHOD Cancel(nsresult) NO_IMPL;
  NS_IMETHOD Suspend() NO_IMPL;
  NS_IMETHOD Resume() NO_IMPL;
  NS_IMETHOD GetLoadGroup(nsILoadGroup**) NO_IMPL;
  NS_IMETHOD SetLoadGroup(nsILoadGroup*) NO_IMPL;
  NS_IMETHOD SetLoadFlags(nsLoadFlags) NO_IMPL;
  NS_IMETHOD GetLoadFlags(nsLoadFlags*) NO_IMPL;
  NS_IMETHOD GetIsDocument(bool*) NO_IMPL;
  NS_IMETHOD GetOriginalURI(nsIURI**) NO_IMPL;
  NS_IMETHOD SetOriginalURI(nsIURI*) NO_IMPL;
  NS_IMETHOD GetURI(nsIURI** aUri) override {
    nsCOMPtr<nsIURI> copy = mUri;
    copy.forget(aUri);
    return NS_OK;
  }
  NS_IMETHOD GetOwner(nsISupports**) NO_IMPL;
  NS_IMETHOD SetOwner(nsISupports*) NO_IMPL;
  NS_IMETHOD GetLoadInfo(nsILoadInfo** aLoadInfo) override {
    nsCOMPtr<nsILoadInfo> copy = mLoadInfo;
    copy.forget(aLoadInfo);
    return NS_OK;
  }
  NS_IMETHOD SetLoadInfo(nsILoadInfo* aLoadInfo) override {
    mLoadInfo = aLoadInfo;
    return NS_OK;
  }
  NS_IMETHOD GetNotificationCallbacks(
      nsIInterfaceRequestor** aRequestor) override {
    NS_ADDREF(*aRequestor = this);
    return NS_OK;
  }
  NS_IMETHOD SetNotificationCallbacks(nsIInterfaceRequestor*) NO_IMPL;
  NS_IMETHOD GetSecurityInfo(nsISupports**) NO_IMPL;
  NS_IMETHOD GetContentType(nsACString&) NO_IMPL;
  NS_IMETHOD SetContentType(const nsACString&) NO_IMPL;
  NS_IMETHOD GetContentCharset(nsACString&) NO_IMPL;
  NS_IMETHOD SetContentCharset(const nsACString&) NO_IMPL;
  NS_IMETHOD GetContentLength(int64_t*) NO_IMPL;
  NS_IMETHOD SetContentLength(int64_t) NO_IMPL;
  NS_IMETHOD Open(nsIInputStream**) NO_IMPL;
  NS_IMETHOD AsyncOpen(nsIStreamListener*) NO_IMPL;
  NS_IMETHOD GetContentDisposition(uint32_t*) NO_IMPL;
  NS_IMETHOD SetContentDisposition(uint32_t) NO_IMPL;
  NS_IMETHOD GetContentDispositionFilename(nsAString&) NO_IMPL;
  NS_IMETHOD SetContentDispositionFilename(const nsAString&) NO_IMPL;
  NS_IMETHOD GetContentDispositionHeader(nsACString&) NO_IMPL;
  NS_IMETHOD OnAuthAvailable(nsISupports* aContext,
                             nsIAuthInformation* aAuthInfo) override;
  NS_IMETHOD OnAuthCancelled(nsISupports* aContext, bool userCancel) override;
  NS_IMETHOD GetInterface(const nsIID& uuid, void** result) override {
    return QueryInterface(uuid, result);
  }
  NS_IMETHOD GetAssociatedWindow(mozIDOMWindowProxy**) NO_IMPL;
  NS_IMETHOD GetTopWindow(mozIDOMWindowProxy**) NO_IMPL;
  NS_IMETHOD GetTopFrameElement(Element** aElement) override {
    RefPtr<Element> elem = mElement;
    elem.forget(aElement);
    return NS_OK;
  }
  NS_IMETHOD GetNestedFrameId(uint64_t*) NO_IMPL;
  NS_IMETHOD GetIsContent(bool*) NO_IMPL;
  NS_IMETHOD GetUsePrivateBrowsing(bool*) NO_IMPL;
  NS_IMETHOD SetUsePrivateBrowsing(bool) NO_IMPL;
  NS_IMETHOD SetPrivateBrowsing(bool) NO_IMPL;
  NS_IMETHOD GetScriptableOriginAttributes(JSContext*,
                                           JS::MutableHandleValue) NO_IMPL;
  NS_IMETHOD_(void)
  GetOriginAttributes(mozilla::OriginAttributes& aAttrs) override {}
  NS_IMETHOD GetUseRemoteTabs(bool*) NO_IMPL;
  NS_IMETHOD SetRemoteTabs(bool) NO_IMPL;
  NS_IMETHOD GetUseTrackingProtection(bool*) NO_IMPL;
  NS_IMETHOD SetUseTrackingProtection(bool) NO_IMPL;
#undef NO_IMPL

 protected:
  ~FakeChannel() {}

  nsCOMPtr<nsIURI> mUri;
  uint64_t mCallbackId;
  RefPtr<Element> mElement;
  nsCOMPtr<nsILoadInfo> mLoadInfo;
};

NS_IMPL_ISUPPORTS(FakeChannel, nsIChannel, nsIAuthPromptCallback, nsIRequest,
                  nsIInterfaceRequestor, nsILoadContext);

mozilla::ipc::IPCResult BrowserParent::RecvAsyncAuthPrompt(
    const nsCString& aUri, const nsString& aRealm,
    const uint64_t& aCallbackId) {
  nsCOMPtr<nsIAuthPrompt2> authPrompt;
  GetAuthPrompt(nsIAuthPromptProvider::PROMPT_NORMAL,
                NS_GET_IID(nsIAuthPrompt2), getter_AddRefs(authPrompt));
  RefPtr<FakeChannel> channel =
      new FakeChannel(aUri, aCallbackId, mFrameElement);
  uint32_t promptFlags = nsIAuthInformation::AUTH_HOST;

  RefPtr<nsAuthInformationHolder> holder =
      new nsAuthInformationHolder(promptFlags, aRealm, EmptyCString());

  uint32_t level = nsIAuthPrompt2::LEVEL_NONE;
  nsCOMPtr<nsICancelable> dummy;
  nsresult rv = authPrompt->AsyncPromptAuth(channel, channel, nullptr, level,
                                            holder, getter_AddRefs(dummy));

  if (NS_FAILED(rv)) {
    return IPC_FAIL_NO_REASON(this);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvInvokeDragSession(
    nsTArray<IPCDataTransfer>&& aTransfers, const uint32_t& aAction,
    Maybe<Shmem>&& aVisualDnDData, const uint32_t& aStride,
    const gfx::SurfaceFormat& aFormat, const LayoutDeviceIntRect& aDragRect,
    nsIPrincipal* aPrincipal) {
  mInitialDataTransferItems.Clear();
  PresShell* presShell = mFrameElement->OwnerDoc()->GetPresShell();
  if (!presShell) {
    Unused << Manager()->SendEndDragSession(true, true, LayoutDeviceIntPoint(),
                                            0);
    // Continue sending input events with input priority when stopping the dnd
    // session.
    Manager()->SetInputPriorityEventEnabled(true);
    return IPC_OK();
  }

  EventStateManager* esm = presShell->GetPresContext()->EventStateManager();
  for (uint32_t i = 0; i < aTransfers.Length(); ++i) {
    mInitialDataTransferItems.AppendElement(std::move(aTransfers[i].items()));
  }

  nsCOMPtr<nsIDragService> dragService =
      do_GetService("@mozilla.org/widget/dragservice;1");
  if (dragService) {
    dragService->MaybeAddChildProcess(Manager());
  }

  if (aVisualDnDData.isNothing() || !aVisualDnDData.ref().IsReadable() ||
      aVisualDnDData.ref().Size<char>() < aDragRect.height * aStride) {
    mDnDVisualization = nullptr;
  } else {
    mDnDVisualization = gfx::CreateDataSourceSurfaceFromData(
        gfx::IntSize(aDragRect.width, aDragRect.height), aFormat,
        aVisualDnDData.ref().get<uint8_t>(), aStride);
  }

  mDragValid = true;
  mDragRect = aDragRect;
  mDragPrincipal = aPrincipal;

  esm->BeginTrackingRemoteDragGesture(mFrameElement);

  if (aVisualDnDData.isSome()) {
    Unused << DeallocShmem(aVisualDnDData.ref());
  }

  return IPC_OK();
}

void BrowserParent::AddInitialDnDDataTo(DataTransfer* aDataTransfer,
                                        nsIPrincipal** aPrincipal) {
  NS_IF_ADDREF(*aPrincipal = mDragPrincipal);

  for (uint32_t i = 0; i < mInitialDataTransferItems.Length(); ++i) {
    nsTArray<IPCDataTransferItem>& itemArray = mInitialDataTransferItems[i];
    for (auto& item : itemArray) {
      RefPtr<nsVariantCC> variant = new nsVariantCC();
      // Special case kFilePromiseMime so that we get the right
      // nsIFlavorDataProvider for it.
      if (item.flavor().EqualsLiteral(kFilePromiseMime)) {
        RefPtr<nsISupports> flavorDataProvider =
            new nsContentAreaDragDropDataProvider();
        variant->SetAsISupports(flavorDataProvider);
      } else if (item.data().type() == IPCDataTransferData::TnsString) {
        variant->SetAsAString(item.data().get_nsString());
      } else if (item.data().type() == IPCDataTransferData::TIPCBlob) {
        RefPtr<BlobImpl> impl =
            IPCBlobUtils::Deserialize(item.data().get_IPCBlob());
        variant->SetAsISupports(impl);
      } else if (item.data().type() == IPCDataTransferData::TShmem) {
        if (nsContentUtils::IsFlavorImage(item.flavor())) {
          // An image! Get the imgIContainer for it and set it in the variant.
          nsCOMPtr<imgIContainer> imageContainer;
          nsresult rv = nsContentUtils::DataTransferItemToImage(
              item, getter_AddRefs(imageContainer));
          if (NS_FAILED(rv)) {
            continue;
          }
          variant->SetAsISupports(imageContainer);
        } else {
          Shmem data = item.data().get_Shmem();
          variant->SetAsACString(
              nsDependentCSubstring(data.get<char>(), data.Size<char>()));
        }

        mozilla::Unused << DeallocShmem(item.data().get_Shmem());
      }

      // We set aHidden to false, as we don't need to worry about hiding data
      // from content in the parent process where there is no content.
      // XXX: Nested Content Processes may change this
      aDataTransfer->SetDataWithPrincipalFromOtherProcess(
          NS_ConvertUTF8toUTF16(item.flavor()), variant, i, mDragPrincipal,
          /* aHidden = */ false);
    }
  }
  mInitialDataTransferItems.Clear();
  mDragPrincipal = nullptr;
}

bool BrowserParent::TakeDragVisualization(
    RefPtr<mozilla::gfx::SourceSurface>& aSurface,
    LayoutDeviceIntRect* aDragRect) {
  if (!mDragValid) return false;

  aSurface = mDnDVisualization.forget();
  *aDragRect = mDragRect;
  mDragValid = false;
  return true;
}

bool BrowserParent::AsyncPanZoomEnabled() const {
  nsCOMPtr<nsIWidget> widget = GetWidget();
  return widget && widget->AsyncPanZoomEnabled();
}

void BrowserParent::StartPersistence(
    uint64_t aOuterWindowID, nsIWebBrowserPersistDocumentReceiver* aRecv,
    ErrorResult& aRv) {
  auto* actor = new WebBrowserPersistDocumentParent();
  actor->SetOnReady(aRecv);
  bool ok = Manager()->SendPWebBrowserPersistDocumentConstructor(
      actor, this, aOuterWindowID);
  if (!ok) {
    aRv.Throw(NS_ERROR_FAILURE);
  }
  // (The actor will be destroyed on constructor failure.)
}

NS_IMETHODIMP
BrowserParent::StartApzAutoscroll(float aAnchorX, float aAnchorY,
                                  nsViewID aScrollId, uint32_t aPresShellId,
                                  bool* aOutRetval) {
  if (!AsyncPanZoomEnabled()) {
    *aOutRetval = false;
    return NS_OK;
  }

  bool success = false;
  if (mRemoteLayerTreeOwner.IsInitialized()) {
    layers::LayersId layersId = mRemoteLayerTreeOwner.GetLayersId();
    if (nsCOMPtr<nsIWidget> widget = GetWidget()) {
      ScrollableLayerGuid guid(layersId, aPresShellId, aScrollId);

      // The anchor coordinates that are passed in are relative to the origin
      // of the screen, but we are sending them to APZ which only knows about
      // coordinates relative to the widget, so convert them accordingly.
      CSSPoint anchorCss{aAnchorX, aAnchorY};
      LayoutDeviceIntPoint anchor =
          RoundedToInt(anchorCss * widget->GetDefaultScale());
      anchor -= widget->WidgetToScreenOffset();

      success = widget->StartAsyncAutoscroll(
          ViewAs<ScreenPixel>(
              anchor, PixelCastJustification::LayoutDeviceIsScreenForBounds),
          guid);
    }
  }
  *aOutRetval = success;
  return NS_OK;
}

NS_IMETHODIMP
BrowserParent::StopApzAutoscroll(nsViewID aScrollId, uint32_t aPresShellId) {
  if (!AsyncPanZoomEnabled()) {
    return NS_OK;
  }

  if (mRemoteLayerTreeOwner.IsInitialized()) {
    layers::LayersId layersId = mRemoteLayerTreeOwner.GetLayersId();
    if (nsCOMPtr<nsIWidget> widget = GetWidget()) {
      ScrollableLayerGuid guid(layersId, aPresShellId, aScrollId);

      widget->StopAsyncAutoscroll(guid);
    }
  }
  return NS_OK;
}

void BrowserParent::SkipBrowsingContextDetach() {
  RefPtr<nsFrameLoader> fl = GetFrameLoader();
  MOZ_ASSERT(fl);
  fl->SkipBrowsingContextDetach();
}

mozilla::ipc::IPCResult BrowserParent::RecvLookUpDictionary(
    const nsString& aText, nsTArray<FontRange>&& aFontRangeArray,
    const bool& aIsVertical, const LayoutDeviceIntPoint& aPoint) {
  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (!widget) {
    return IPC_OK();
  }

  widget->LookUpDictionary(aText, aFontRangeArray, aIsVertical,
                           TransformChildToParent(aPoint));
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvShowCanvasPermissionPrompt(
    const nsCString& aOrigin, const bool& aHideDoorHanger) {
  nsCOMPtr<nsIBrowser> browser =
      mFrameElement ? mFrameElement->AsBrowser() : nullptr;
  if (!browser) {
    // If the tab is being closed, the browser may not be available.
    // In this case we can ignore the request.
    return IPC_OK();
  }
  nsCOMPtr<nsIObserverService> os = services::GetObserverService();
  if (!os) {
    return IPC_FAIL_NO_REASON(this);
  }
  nsresult rv = os->NotifyObservers(
      browser,
      aHideDoorHanger ? "canvas-permissions-prompt-hide-doorhanger"
                      : "canvas-permissions-prompt",
      NS_ConvertUTF8toUTF16(aOrigin).get());
  if (NS_FAILED(rv)) {
    return IPC_FAIL_NO_REASON(this);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvVisitURI(nsIURI* aURI,
                                                    nsIURI* aLastVisitedURI,
                                                    const uint32_t& aFlags) {
  if (!aURI) {
    return IPC_FAIL_NO_REASON(this);
  }
  RefPtr<nsIWidget> widget = GetWidget();
  if (NS_WARN_IF(!widget)) {
    return IPC_OK();
  }
  nsCOMPtr<IHistory> history = services::GetHistoryService();
  if (history) {
    Unused << history->VisitURI(widget, aURI, aLastVisitedURI, aFlags);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvQueryVisitedState(
    const nsTArray<RefPtr<nsIURI>>&& aURIs) {
#ifdef MOZ_ANDROID_HISTORY
  nsCOMPtr<IHistory> history = services::GetHistoryService();
  if (NS_WARN_IF(!history)) {
    return IPC_OK();
  }
  RefPtr<nsIWidget> widget = GetWidget();
  if (NS_WARN_IF(!widget)) {
    return IPC_OK();
  }

  for (size_t i = 0; i < aURIs.Length(); ++i) {
    if (!aURIs[i]) {
      return IPC_FAIL(this, "Received null URI");
    }
  }

  GeckoViewHistory* gvHistory = static_cast<GeckoViewHistory*>(history.get());
  gvHistory->QueryVisitedState(widget, std::move(aURIs));

  return IPC_OK();
#else
  return IPC_FAIL(this, "QueryVisitedState is Android-only");
#endif
}

void BrowserParent::LiveResizeStarted() { SuppressDisplayport(true); }

void BrowserParent::LiveResizeStopped() { SuppressDisplayport(false); }

mozilla::ipc::IPCResult BrowserParent::RecvSetSystemFont(
    const nsCString& aFontName) {
  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (widget) {
    widget->SetSystemFont(aFontName);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserParent::RecvGetSystemFont(nsCString* aFontName) {
  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (widget) {
    widget->GetSystemFont(*aFontName);
  }
  return IPC_OK();
}

NS_IMETHODIMP
FakeChannel::OnAuthAvailable(nsISupports* aContext,
                             nsIAuthInformation* aAuthInfo) {
  nsAuthInformationHolder* holder =
      static_cast<nsAuthInformationHolder*>(aAuthInfo);

  if (!net::gNeckoChild->SendOnAuthAvailable(
          mCallbackId, holder->User(), holder->Password(), holder->Domain())) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

NS_IMETHODIMP
FakeChannel::OnAuthCancelled(nsISupports* aContext, bool userCancel) {
  if (!net::gNeckoChild->SendOnAuthCancelled(mCallbackId, userCancel)) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

}  // namespace dom
}  // namespace mozilla
