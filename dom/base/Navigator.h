/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_Navigator_h
#define mozilla_dom_Navigator_h

#include "mozilla/MemoryReporting.h"
#include "mozilla/dom/AddonManagerBinding.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/Fetch.h"
#include "mozilla/dom/Nullable.h"
#include "nsWrapperCache.h"
#include "nsHashKeys.h"
#include "nsInterfaceHashtable.h"
#include "nsString.h"
#include "nsTArray.h"

class nsPluginArray;
class nsMimeTypeArray;
class nsPIDOMWindowInner;
class nsIDOMNavigatorSystemMessages;
class nsIPrincipal;
class nsIURI;

namespace mozilla {
class ErrorResult;

namespace dom {
class AddonManager;
class BodyExtractorBase;
class Geolocation;
class systemMessageCallback;
class MediaDevices;
struct MediaStreamConstraints;
class WakeLock;
class ArrayBufferOrArrayBufferViewOrBlobOrFormDataOrUSVStringOrURLSearchParams;
class ServiceWorkerContainer;
class DOMRequest;
class CredentialsContainer;
class Clipboard;
}  // namespace dom
#ifdef MOZ_WEBGPU
namespace webgpu {
class Instance;
}  // namespace webgpu
#endif
}  // namespace mozilla
//*****************************************************************************
// Navigator: Script "navigator" object
//*****************************************************************************

namespace mozilla {
namespace dom {

class Permissions;

namespace battery {
class BatteryManager;
}  // namespace battery

class Promise;

class Gamepad;
class GamepadServiceTest;
class NavigatorUserMediaSuccessCallback;
class NavigatorUserMediaErrorCallback;
class MozGetUserMediaDevicesSuccessCallback;

struct MIDIOptions;

namespace network {
class Connection;
}  // namespace network

class Presentation;
class LegacyMozTCPSocket;
#ifdef MOZ_VR
class VRDisplay;
class VRServiceTest;
#endif
class StorageManager;
class MediaCapabilities;

class Navigator final : public nsISupports, public nsWrapperCache {
 public:
  explicit Navigator(nsPIDOMWindowInner* aInnerWindow);

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(Navigator)

  void Invalidate();
  nsPIDOMWindowInner* GetWindow() const { return mWindow; }

  void RefreshMIMEArray();

  size_t SizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf) const;

  /**
   * For use during document.write where our inner window changes.
   */
  void SetWindow(nsPIDOMWindowInner* aInnerWindow);

  /**
   * Called when the inner window navigates to a new page.
   */
  void OnNavigation();

  void GetProduct(nsAString& aProduct);
  void GetLanguage(nsAString& aLanguage);
  void GetAppName(nsAString& aAppName, CallerType aCallerType) const;
  void GetAppVersion(nsAString& aAppName, CallerType aCallerType,
                     ErrorResult& aRv) const;
  void GetPlatform(nsAString& aPlatform, CallerType aCallerType,
                   ErrorResult& aRv) const;
  void GetUserAgent(nsAString& aUserAgent, CallerType aCallerType,
                    ErrorResult& aRv) const;
  bool OnLine();
  void CheckProtocolHandlerAllowed(const nsAString& aScheme,
                                   nsIURI* aHandlerURI, nsIURI* aDocumentURI,
                                   ErrorResult& aRv);
  void RegisterProtocolHandler(const nsAString& aScheme, const nsAString& aURL,
                               const nsAString& aTitle, ErrorResult& aRv);
  nsMimeTypeArray* GetMimeTypes(ErrorResult& aRv);
  nsPluginArray* GetPlugins(ErrorResult& aRv);
  Permissions* GetPermissions(ErrorResult& aRv);
  void GetDoNotTrack(nsAString& aResult);
  Geolocation* GetGeolocation(ErrorResult& aRv);
  Promise* GetBattery(ErrorResult& aRv);

  static void AppName(nsAString& aAppName, nsIPrincipal* aCallerPrincipal,
                      bool aUsePrefOverriddenValue);

  static nsresult GetPlatform(nsAString& aPlatform,
                              nsIPrincipal* aCallerPrincipal,
                              bool aUsePrefOverriddenValue);

  static nsresult GetAppVersion(nsAString& aAppVersion,
                                nsIPrincipal* aCallerPrincipal,
                                bool aUsePrefOverriddenValue);

  static nsresult GetUserAgent(nsPIDOMWindowInner* aWindow,
                               nsIPrincipal* aCallerPrincipal,
                               bool aIsCallerChrome, nsAString& aUserAgent);

  // Clears the user agent cache by calling:
  // Navigator_Binding::ClearCachedUserAgentValue(this);
  void ClearUserAgentCache();

  bool Vibrate(uint32_t aDuration);
  bool Vibrate(const nsTArray<uint32_t>& aDuration);
  void SetVibrationPermission(bool aPermitted, bool aPersistent);
  uint32_t MaxTouchPoints(CallerType aCallerType);
  void GetAppCodeName(nsAString& aAppCodeName, ErrorResult& aRv);
  void GetOscpu(nsAString& aOscpu, CallerType aCallerType,
                ErrorResult& aRv) const;
  void GetVendorSub(nsAString& aVendorSub);
  void GetVendor(nsAString& aVendor);
  void GetProductSub(nsAString& aProductSub);
  bool CookieEnabled();
  void GetBuildID(nsAString& aBuildID, CallerType aCallerType,
                  ErrorResult& aRv) const;
  bool JavaEnabled() { return false; }
  uint64_t HardwareConcurrency();
  bool TaintEnabled() { return false; }

  already_AddRefed<LegacyMozTCPSocket> MozTCPSocket();
  network::Connection* GetConnection(ErrorResult& aRv);
  MediaDevices* GetMediaDevices(ErrorResult& aRv);

  void GetGamepads(nsTArray<RefPtr<Gamepad>>& aGamepads, ErrorResult& aRv);
  GamepadServiceTest* RequestGamepadServiceTest();
#ifdef MOZ_VR
  already_AddRefed<Promise> GetVRDisplays(ErrorResult& aRv);
  void GetActiveVRDisplays(nsTArray<RefPtr<VRDisplay>>& aDisplays) const;
  VRServiceTest* RequestVRServiceTest();
  bool IsWebVRContentDetected() const;
  bool IsWebVRContentPresenting() const;
  void RequestVRPresentation(VRDisplay& aDisplay);
#endif
  already_AddRefed<Promise> RequestMIDIAccess(const MIDIOptions& aOptions,
                                              ErrorResult& aRv);

  Presentation* GetPresentation(ErrorResult& aRv);

  bool SendBeacon(const nsAString& aUrl, const Nullable<fetch::BodyInit>& aData,
                  ErrorResult& aRv);

  void MozGetUserMedia(const MediaStreamConstraints& aConstraints,
                       NavigatorUserMediaSuccessCallback& aOnSuccess,
                       NavigatorUserMediaErrorCallback& aOnError,
                       CallerType aCallerType, ErrorResult& aRv);
  MOZ_CAN_RUN_SCRIPT
  void MozGetUserMediaDevices(const MediaStreamConstraints& aConstraints,
                              MozGetUserMediaDevicesSuccessCallback& aOnSuccess,
                              NavigatorUserMediaErrorCallback& aOnError,
                              uint64_t aInnerWindowID, const nsAString& aCallID,
                              ErrorResult& aRv);

  already_AddRefed<ServiceWorkerContainer> ServiceWorker();

  mozilla::dom::CredentialsContainer* Credentials();
  dom::Clipboard* Clipboard();
#ifdef MOZ_WEBGPU
  webgpu::Instance* Gpu();
#endif

  static bool Webdriver();

  void GetLanguages(nsTArray<nsString>& aLanguages);

  StorageManager* Storage();

  static void GetAcceptLanguages(nsTArray<nsString>& aLanguages);

  dom::MediaCapabilities* MediaCapabilities();

  AddonManager* GetMozAddonManager(ErrorResult& aRv);

  // WebIDL helper methods
  static bool HasUserMediaSupport(JSContext* /* unused */,
                                  JSObject* /* unused */);

  nsPIDOMWindowInner* GetParentObject() const { return GetWindow(); }

  virtual JSObject* WrapObject(JSContext* cx,
                               JS::Handle<JSObject*> aGivenProto) override;

  // GetWindowFromGlobal returns the inner window for this global, if
  // any, else null.
  static already_AddRefed<nsPIDOMWindowInner> GetWindowFromGlobal(
      JSObject* aGlobal);

#ifdef MOZ_VR
 public:
  void NotifyVRDisplaysUpdated();
  void NotifyActiveVRDisplaysChanged();
#endif

 private:
  virtual ~Navigator();

  // This enum helps SendBeaconInternal to apply different behaviors to body
  // types.
  enum BeaconType { eBeaconTypeBlob, eBeaconTypeArrayBuffer, eBeaconTypeOther };

  bool SendBeaconInternal(const nsAString& aUrl, BodyExtractorBase* aBody,
                          BeaconType aType, ErrorResult& aRv);

  nsIDocShell* GetDocShell() const {
    return mWindow ? mWindow->GetDocShell() : nullptr;
  }

  RefPtr<nsMimeTypeArray> mMimeTypes;
  RefPtr<nsPluginArray> mPlugins;
  RefPtr<Permissions> mPermissions;
  RefPtr<Geolocation> mGeolocation;
  RefPtr<battery::BatteryManager> mBatteryManager;
  RefPtr<Promise> mBatteryPromise;
  RefPtr<network::Connection> mConnection;
  RefPtr<CredentialsContainer> mCredentials;
  RefPtr<dom::Clipboard> mClipboard;
  RefPtr<MediaDevices> mMediaDevices;
  RefPtr<ServiceWorkerContainer> mServiceWorkerContainer;
  nsCOMPtr<nsPIDOMWindowInner> mWindow;
  RefPtr<Presentation> mPresentation;
  RefPtr<GamepadServiceTest> mGamepadServiceTest;
#ifdef MOZ_VR
  nsTArray<RefPtr<Promise>> mVRGetDisplaysPromises;
  RefPtr<VRServiceTest> mVRServiceTest;
#endif
  nsTArray<uint32_t> mRequestedVibrationPattern;
  RefPtr<StorageManager> mStorageManager;
  RefPtr<dom::MediaCapabilities> mMediaCapabilities;
  RefPtr<AddonManager> mAddonManager;
#ifdef MOZ_WEBGPU
  RefPtr<webgpu::Instance> mWebGpu;
#endif
};

}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_Navigator_h
