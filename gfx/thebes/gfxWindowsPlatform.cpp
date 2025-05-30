/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#define INITGUID  // set before devguid.h

#include "gfxWindowsPlatform.h"

#include "cairo.h"
#include "mozilla/ArrayUtils.h"
#include "mozilla/layers/CompositorBridgeChild.h"

#include "gfxImageSurface.h"
#include "gfxWindowsSurface.h"

#include "nsUnicharUtils.h"
#include "nsUnicodeProperties.h"

#include "mozilla/Preferences.h"
#include "mozilla/Services.h"
#include "mozilla/Sprintf.h"
#include "mozilla/WindowsVersion.h"
#include "nsIGfxInfo.h"
#include "nsServiceManagerUtils.h"
#include "nsTArray.h"
#include "nsThreadUtils.h"
#include "mozilla/Telemetry.h"
#include "GeckoProfiler.h"

#include "plbase64.h"
#include "nsIXULRuntime.h"
#include "imgLoader.h"

#include "nsIGfxInfo.h"

#include "gfxCrashReporterUtils.h"

#include "gfxGDIFontList.h"
#include "gfxGDIFont.h"

#include "mozilla/layers/CompositorThread.h"
#include "DeviceManagerD3D9.h"
#include "mozilla/layers/PaintThread.h"
#include "mozilla/layers/ReadbackManagerD3D11.h"

#include "WinUtils.h"

#include "gfxDWriteFontList.h"
#include "gfxDWriteFonts.h"
#include "gfxDWriteCommon.h"
#include <dwrite.h>

#include "gfxTextRun.h"
#include "gfxUserFontSet.h"
#include "nsWindowsHelpers.h"
#include "gfx2DGlue.h"

#include <string>

#include <d3d10_1.h>

#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/gfxVars.h"

#include "nsMemory.h"

#include <dwmapi.h>
#include <d3d11.h>
#include <d2d1_1.h>

#include "nsIMemoryReporter.h"
#include <winternl.h>
#include "d3dkmtQueryStatistics.h"

#include "base/thread.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/StaticPrefs_layers.h"
#include "gfxConfig.h"
#include "VsyncSource.h"
#include "DriverCrashGuard.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/gfx/DeviceManagerDx.h"
#include "mozilla/layers/DeviceAttachmentsD3D11.h"
#include "D3D11Checks.h"

#include <devguid.h>   // for GUID_DEVCLASS_BATTERY
#include <setupapi.h>  // for SetupDi*
#include <winioctl.h>  // for IOCTL_*
#include <batclass.h>  // for BATTERY_*

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::layers;
using namespace mozilla::widget;
using namespace mozilla::image;
using namespace mozilla::unicode;

DCForMetrics::DCForMetrics() {
  // Get the whole screen DC:
  mDC = GetDC(nullptr);
  SetGraphicsMode(mDC, GM_ADVANCED);
}

class GfxD2DVramReporter final : public nsIMemoryReporter {
  ~GfxD2DVramReporter() {}

 public:
  NS_DECL_ISUPPORTS

  NS_IMETHOD CollectReports(nsIHandleReportCallback* aHandleReport,
                            nsISupports* aData, bool aAnonymize) override {
    MOZ_COLLECT_REPORT("gfx-d2d-vram-draw-target", KIND_OTHER, UNITS_BYTES,
                       Factory::GetD2DVRAMUsageDrawTarget(),
                       "Video memory used by D2D DrawTargets.");

    MOZ_COLLECT_REPORT("gfx-d2d-vram-source-surface", KIND_OTHER, UNITS_BYTES,
                       Factory::GetD2DVRAMUsageSourceSurface(),
                       "Video memory used by D2D SourceSurfaces.");

    return NS_OK;
  }
};

NS_IMPL_ISUPPORTS(GfxD2DVramReporter, nsIMemoryReporter)

#define GFX_USE_CLEARTYPE_ALWAYS \
  "gfx.font_rendering.cleartype.always_use_for_content"
#define GFX_DOWNLOADABLE_FONTS_USE_CLEARTYPE \
  "gfx.font_rendering.cleartype.use_for_downloadable_fonts"

#define GFX_CLEARTYPE_PARAMS "gfx.font_rendering.cleartype_params."
#define GFX_CLEARTYPE_PARAMS_GAMMA "gfx.font_rendering.cleartype_params.gamma"
#define GFX_CLEARTYPE_PARAMS_CONTRAST \
  "gfx.font_rendering.cleartype_params.enhanced_contrast"
#define GFX_CLEARTYPE_PARAMS_LEVEL \
  "gfx.font_rendering.cleartype_params.cleartype_level"
#define GFX_CLEARTYPE_PARAMS_STRUCTURE \
  "gfx.font_rendering.cleartype_params.pixel_structure"
#define GFX_CLEARTYPE_PARAMS_MODE \
  "gfx.font_rendering.cleartype_params.rendering_mode"

class GPUAdapterReporter final : public nsIMemoryReporter {
  // Callers must Release the DXGIAdapter after use or risk mem-leak
  static bool GetDXGIAdapter(IDXGIAdapter** aDXGIAdapter) {
    RefPtr<ID3D11Device> d3d11Device;
    RefPtr<IDXGIDevice> dxgiDevice;
    bool result = false;

    if ((d3d11Device = mozilla::gfx::Factory::GetDirect3D11Device())) {
      if (d3d11Device->QueryInterface(__uuidof(IDXGIDevice),
                                      getter_AddRefs(dxgiDevice)) == S_OK) {
        result = (dxgiDevice->GetAdapter(aDXGIAdapter) == S_OK);
      }
    }

    return result;
  }

  ~GPUAdapterReporter() {}

 public:
  NS_DECL_ISUPPORTS

  NS_IMETHOD
  CollectReports(nsIHandleReportCallback* aHandleReport, nsISupports* aData,
                 bool aAnonymize) override {
    HANDLE ProcessHandle = GetCurrentProcess();

    int64_t dedicatedBytesUsed = 0;
    int64_t sharedBytesUsed = 0;
    int64_t committedBytesUsed = 0;
    IDXGIAdapter* DXGIAdapter;

    HMODULE gdi32Handle;
    PFND3DKMTQS queryD3DKMTStatistics = nullptr;

    if ((gdi32Handle = LoadLibrary(TEXT("gdi32.dll"))))
      queryD3DKMTStatistics =
          (PFND3DKMTQS)GetProcAddress(gdi32Handle, "D3DKMTQueryStatistics");

    if (queryD3DKMTStatistics && GetDXGIAdapter(&DXGIAdapter)) {
      // Most of this block is understood thanks to wj32's work on Process
      // Hacker

      DXGI_ADAPTER_DESC adapterDesc;
      D3DKMTQS queryStatistics;

      DXGIAdapter->GetDesc(&adapterDesc);
      DXGIAdapter->Release();

      memset(&queryStatistics, 0, sizeof(D3DKMTQS));
      queryStatistics.Type = D3DKMTQS_PROCESS;
      queryStatistics.AdapterLuid = adapterDesc.AdapterLuid;
      queryStatistics.hProcess = ProcessHandle;
      if (NT_SUCCESS(queryD3DKMTStatistics(&queryStatistics))) {
        committedBytesUsed =
            queryStatistics.QueryResult.ProcessInfo.SystemMemory.BytesAllocated;
      }

      memset(&queryStatistics, 0, sizeof(D3DKMTQS));
      queryStatistics.Type = D3DKMTQS_ADAPTER;
      queryStatistics.AdapterLuid = adapterDesc.AdapterLuid;
      if (NT_SUCCESS(queryD3DKMTStatistics(&queryStatistics))) {
        ULONG i;
        ULONG segmentCount = queryStatistics.QueryResult.AdapterInfo.NbSegments;

        for (i = 0; i < segmentCount; i++) {
          memset(&queryStatistics, 0, sizeof(D3DKMTQS));
          queryStatistics.Type = D3DKMTQS_SEGMENT;
          queryStatistics.AdapterLuid = adapterDesc.AdapterLuid;
          queryStatistics.QuerySegment.SegmentId = i;

          if (NT_SUCCESS(queryD3DKMTStatistics(&queryStatistics))) {
            bool aperture;

            // SegmentInformation has a different definition in Win7 than later
            // versions
            if (!IsWin8OrLater())
              aperture = queryStatistics.QueryResult.SegmentInfoWin7.Aperture;
            else
              aperture = queryStatistics.QueryResult.SegmentInfoWin8.Aperture;

            memset(&queryStatistics, 0, sizeof(D3DKMTQS));
            queryStatistics.Type = D3DKMTQS_PROCESS_SEGMENT;
            queryStatistics.AdapterLuid = adapterDesc.AdapterLuid;
            queryStatistics.hProcess = ProcessHandle;
            queryStatistics.QueryProcessSegment.SegmentId = i;
            if (NT_SUCCESS(queryD3DKMTStatistics(&queryStatistics))) {
              ULONGLONG bytesCommitted;
              if (!IsWin8OrLater())
                bytesCommitted = queryStatistics.QueryResult.ProcessSegmentInfo
                                     .Win7.BytesCommitted;
              else
                bytesCommitted = queryStatistics.QueryResult.ProcessSegmentInfo
                                     .Win8.BytesCommitted;
              if (aperture)
                sharedBytesUsed += bytesCommitted;
              else
                dedicatedBytesUsed += bytesCommitted;
            }
          }
        }
      }
    }

    FreeLibrary(gdi32Handle);

    MOZ_COLLECT_REPORT("gpu-committed", KIND_OTHER, UNITS_BYTES,
                       committedBytesUsed,
                       "Memory committed by the Windows graphics system.");

    MOZ_COLLECT_REPORT(
        "gpu-dedicated", KIND_OTHER, UNITS_BYTES, dedicatedBytesUsed,
        "Out-of-process memory allocated for this process in a physical "
        "GPU adapter's memory.");

    MOZ_COLLECT_REPORT("gpu-shared", KIND_OTHER, UNITS_BYTES, sharedBytesUsed,
                       "In-process memory that is shared with the GPU.");

    return NS_OK;
  }
};

NS_IMPL_ISUPPORTS(GPUAdapterReporter, nsIMemoryReporter)

Atomic<size_t> gfxWindowsPlatform::sD3D11SharedTextures;
Atomic<size_t> gfxWindowsPlatform::sD3D9SharedTextures;

class D3DSharedTexturesReporter final : public nsIMemoryReporter {
  ~D3DSharedTexturesReporter() {}

 public:
  NS_DECL_ISUPPORTS

  NS_IMETHOD CollectReports(nsIHandleReportCallback* aHandleReport,
                            nsISupports* aData, bool aAnonymize) override {
    if (gfxWindowsPlatform::sD3D11SharedTextures > 0) {
      MOZ_COLLECT_REPORT("d3d11-shared-textures", KIND_OTHER, UNITS_BYTES,
                         gfxWindowsPlatform::sD3D11SharedTextures,
                         "D3D11 shared textures.");
    }

    if (gfxWindowsPlatform::sD3D9SharedTextures > 0) {
      MOZ_COLLECT_REPORT("d3d9-shared-textures", KIND_OTHER, UNITS_BYTES,
                         gfxWindowsPlatform::sD3D9SharedTextures,
                         "D3D9 shared textures.");
    }

    return NS_OK;
  }
};

NS_IMPL_ISUPPORTS(D3DSharedTexturesReporter, nsIMemoryReporter)

gfxWindowsPlatform::gfxWindowsPlatform() : mRenderMode(RENDER_GDI) {
  mUseClearTypeForDownloadableFonts = UNINITIALIZED_VALUE;
  mUseClearTypeAlways = UNINITIALIZED_VALUE;
  /*
   * Initialize COM
   */
  CoInitialize(nullptr);

  RegisterStrongMemoryReporter(new GfxD2DVramReporter());
  RegisterStrongMemoryReporter(new GPUAdapterReporter());
  RegisterStrongMemoryReporter(new D3DSharedTexturesReporter());
}

gfxWindowsPlatform::~gfxWindowsPlatform() {
  mozilla::gfx::Factory::D2DCleanup();

  DeviceManagerD3D9::Shutdown();
  DeviceManagerDx::Shutdown();

  /*
   * Uninitialize COM
   */
  CoUninitialize();
}

static void UpdateANGLEConfig() {
  if (!gfxConfig::IsEnabled(Feature::D3D11_COMPOSITING)) {
    gfxConfig::Disable(Feature::D3D11_HW_ANGLE, FeatureStatus::Disabled,
                       "D3D11 compositing is disabled");
  }
}

bool gfxWindowsPlatform::HasBattery() {
  // Helper classes to manage lifetimes of Windows structs.
  class MOZ_STACK_CLASS HDevInfoHolder final {
   public:
    explicit HDevInfoHolder(HDEVINFO aHandle) : mHandle(aHandle) {}

    ~HDevInfoHolder() { ::SetupDiDestroyDeviceInfoList(mHandle); }

   private:
    HDEVINFO mHandle;
  };

  class MOZ_STACK_CLASS HandleHolder final {
   public:
    explicit HandleHolder(HANDLE aHandle) : mHandle(aHandle) {}

    ~HandleHolder() { ::CloseHandle(mHandle); }

   private:
    HANDLE mHandle;
  };

  HDEVINFO hdev =
      ::SetupDiGetClassDevs(&GUID_DEVCLASS_BATTERY, nullptr, nullptr,
                            DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
  if (hdev == INVALID_HANDLE_VALUE) {
    return true;
  }

  HDevInfoHolder hdevHolder(hdev);

  DWORD i = 0;
  SP_DEVICE_INTERFACE_DATA did = {0};
  did.cbSize = sizeof(did);

  while (::SetupDiEnumDeviceInterfaces(hdev, nullptr, &GUID_DEVCLASS_BATTERY, i,
                                       &did)) {
    DWORD bufferSize = 0;
    ::SetupDiGetDeviceInterfaceDetail(hdev, &did, nullptr, 0, &bufferSize,
                                      nullptr);
    if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
      return true;
    }

    UniquePtr<uint8_t[]> buffer(new (std::nothrow) uint8_t[bufferSize]);
    if (!buffer) {
      return true;
    }

    PSP_DEVICE_INTERFACE_DETAIL_DATA pdidd =
        reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA>(buffer.get());
    pdidd->cbSize = sizeof(*pdidd);
    if (!::SetupDiGetDeviceInterfaceDetail(hdev, &did, pdidd, bufferSize,
                                           &bufferSize, nullptr)) {
      return true;
    }

    HANDLE hbat = ::CreateFile(pdidd->DevicePath, GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hbat == INVALID_HANDLE_VALUE) {
      return true;
    }

    HandleHolder hbatHolder(hbat);

    BATTERY_QUERY_INFORMATION bqi = {0};
    DWORD dwWait = 0;
    DWORD dwOut;

    // We need the tag to query the information below.
    if (!::DeviceIoControl(hbat, IOCTL_BATTERY_QUERY_TAG, &dwWait,
                           sizeof(dwWait), &bqi.BatteryTag,
                           sizeof(bqi.BatteryTag), &dwOut, nullptr) ||
        !bqi.BatteryTag) {
      return true;
    }

    BATTERY_INFORMATION bi = {0};
    bqi.InformationLevel = BatteryInformation;

    if (!::DeviceIoControl(hbat, IOCTL_BATTERY_QUERY_INFORMATION, &bqi,
                           sizeof(bqi), &bi, sizeof(bi), &dwOut, nullptr)) {
      return true;
    }

    // If a battery intended for general use (i.e. system use) is not a UPS
    // (i.e. short term), then we know for certain we have a battery.
    if ((bi.Capabilities & BATTERY_SYSTEM_BATTERY) &&
        !(bi.Capabilities & BATTERY_IS_SHORT_TERM)) {
      return true;
    }

    // Otherwise we check the next battery.
    ++i;
  }

  // If we fail to enumerate because there are no more batteries to check, then
  // we can safely say there are indeed no system batteries.
  return ::GetLastError() != ERROR_NO_MORE_ITEMS;
}

void gfxWindowsPlatform::InitAcceleration() {
  gfxPlatform::InitAcceleration();

  DeviceManagerDx::Init();
  DeviceManagerD3D9::Init();

  InitializeConfig();
  // Ensure devices initialization. SharedSurfaceANGLE and
  // SharedSurfaceD3D11Interop use them. The devices are lazily initialized
  // with WebRender to reduce memory usage.
  // Initialize them now when running non-e10s.
  if (!BrowserTabsRemoteAutostart()) {
    EnsureDevicesInitialized();
  }
  UpdateANGLEConfig();
  UpdateRenderMode();

  // If we have Skia and we didn't init dwrite already, do it now.
  if (!DWriteEnabled() && GetDefaultContentBackend() == BackendType::SKIA) {
    InitDWriteSupport();
  }
  // We need to listen for font setting changes even if DWrite is not used.
  Factory::SetSystemTextQuality(gfxVars::SystemTextQuality());
  gfxVars::SetSystemTextQualityListener(
      gfxDWriteFont::SystemTextQualityChanged);

  // CanUseHardwareVideoDecoding depends on DeviceManagerDx state,
  // so update the cached value now.
  UpdateCanUseHardwareVideoDecoding();
}

#ifdef MOZ_BUILD_WEBRENDER
void gfxWindowsPlatform::InitWebRenderConfig() {
  gfxPlatform::InitWebRenderConfig();

  if (gfxVars::UseWebRender()) {
    UpdateBackendPrefs();
  }
}
#endif

bool gfxWindowsPlatform::CanUseHardwareVideoDecoding() {
  DeviceManagerDx* dm = DeviceManagerDx::Get();
  if (!dm) {
    return false;
  }
  if (!StaticPrefs::layers_prefer_d3d9_AtStartup() &&
      !dm->TextureSharingWorks()) {
    return false;
  }
  return !dm->IsWARP() && gfxPlatform::CanUseHardwareVideoDecoding();
}

bool gfxWindowsPlatform::InitDWriteSupport() {
  if (!IsVistaOrLater()) {
    return false;
  }

  // DWrite is only supported on Windows 7 with the platform update and higher.
  // We check this by seeing if D2D1 support is available.
  if (!Factory::SupportsD2D1()) {
    return false;
  }

  mozilla::ScopedGfxFeatureReporter reporter("DWrite");
  if (!Factory::EnsureDWriteFactory()) {
    return false;
  }

  SetupClearTypeParams();
  reporter.SetSuccessful();
  return true;
}

bool gfxWindowsPlatform::HandleDeviceReset() {
  DeviceResetReason resetReason = DeviceResetReason::OK;
  if (!DidRenderingDeviceReset(&resetReason)) {
    return false;
  }

  if (resetReason != DeviceResetReason::FORCED_RESET) {
    Telemetry::Accumulate(Telemetry::DEVICE_RESET_REASON,
                          uint32_t(resetReason));
  }

  // Remove devices and adapters.
  DeviceManagerDx::Get()->ResetDevices();

  imgLoader::NormalLoader()->ClearCache(true);
  imgLoader::NormalLoader()->ClearCache(false);
  imgLoader::PrivateBrowsingLoader()->ClearCache(true);
  imgLoader::PrivateBrowsingLoader()->ClearCache(false);
  gfxAlphaBoxBlur::ShutdownBlurCache();

  gfxConfig::Reset(Feature::D3D11_COMPOSITING);
  gfxConfig::Reset(Feature::ADVANCED_LAYERS);
  gfxConfig::Reset(Feature::D3D11_HW_ANGLE);
  gfxConfig::Reset(Feature::DIRECT2D);

  InitializeConfig();
  if (mInitializedDevices) {
    InitializeDevices();
  }
  UpdateANGLEConfig();
  return true;
}

BackendPrefsData gfxWindowsPlatform::GetBackendPrefs() const {
  BackendPrefsData data;

  data.mCanvasBitmask =
      BackendTypeBit(BackendType::CAIRO) | BackendTypeBit(BackendType::SKIA);
  data.mContentBitmask =
      BackendTypeBit(BackendType::CAIRO) | BackendTypeBit(BackendType::SKIA);
  data.mCanvasDefault = BackendType::SKIA;
  data.mContentDefault = BackendType::SKIA;

  if (gfxConfig::IsEnabled(Feature::DIRECT2D)) {
    data.mCanvasBitmask |= BackendTypeBit(BackendType::DIRECT2D1_1);
    data.mCanvasDefault = BackendType::DIRECT2D1_1;
#ifdef MOZ_BUILD_WEBRENDER
    // We do not use d2d for content when WebRender is used.
    if (!gfxVars::UseWebRender()) {
#endif
      data.mContentBitmask |= BackendTypeBit(BackendType::DIRECT2D1_1);
      data.mContentDefault = BackendType::DIRECT2D1_1;
#ifdef MOZ_BUILD_WEBRENDER
    }
#endif
  }
  return data;
}

void gfxWindowsPlatform::UpdateBackendPrefs() {
  BackendPrefsData data = GetBackendPrefs();
  // Remove DIRECT2D1 preference if D2D1Device does not exist.
  if (!Factory::HasD2D1Device()) {
    data.mCanvasBitmask &= ~BackendTypeBit(BackendType::DIRECT2D1_1);
    data.mContentBitmask &= ~BackendTypeBit(BackendType::DIRECT2D1_1);
    if (data.mCanvasDefault == BackendType::DIRECT2D1_1) {
      data.mCanvasDefault = BackendType::SKIA;
    }
    if (data.mContentDefault == BackendType::DIRECT2D1_1) {
      data.mContentDefault = BackendType::SKIA;
    }
  }
  InitBackendPrefs(std::move(data));
}

bool gfxWindowsPlatform::IsDirect2DBackend() {
  return GetDefaultContentBackend() == BackendType::DIRECT2D1_1;
}

void gfxWindowsPlatform::UpdateRenderMode() {
  bool didReset = HandleDeviceReset();

  UpdateBackendPrefs();

  if (PaintThread::Get()) {
    PaintThread::Get()->UpdateRenderMode();
  }

  if (didReset) {
    mScreenReferenceDrawTarget = CreateOffscreenContentDrawTarget(
        IntSize(1, 1), SurfaceFormat::B8G8R8A8);
    if (!mScreenReferenceDrawTarget) {
      gfxCriticalNote
          << "Failed to update reference draw target after device reset"
          << ", D3D11 device:" << hexa(Factory::GetDirect3D11Device().get())
          << ", D3D11 status:"
          << FeatureStatusToString(
                 gfxConfig::GetValue(Feature::D3D11_COMPOSITING))
          << ", D2D1 device:" << hexa(Factory::GetD2D1Device().get())
          << ", D2D1 status:"
          << FeatureStatusToString(gfxConfig::GetValue(Feature::DIRECT2D))
          << ", content:" << int(GetDefaultContentBackend())
          << ", compositor:" << int(GetCompositorBackend());
      MOZ_CRASH(
          "GFX: Failed to update reference draw target after device reset");
    }
  }
}

mozilla::gfx::BackendType gfxWindowsPlatform::GetContentBackendFor(
    mozilla::layers::LayersBackend aLayers) {
  mozilla::gfx::BackendType defaultBackend =
      gfxPlatform::GetDefaultContentBackend();
  if (aLayers == LayersBackend::LAYERS_D3D11) {
    return defaultBackend;
  }

#ifdef MOZ_BUILD_WEBRENDER
  if (aLayers == LayersBackend::LAYERS_WR &&
      gfx::gfxVars::UseWebRenderANGLE()) {
    return defaultBackend;
  }
#endif

  if (defaultBackend == BackendType::DIRECT2D1_1) {
    // We can't have D2D without D3D11 layers, so fallback to Skia.
    return BackendType::SKIA;
  }

  // Otherwise we have some non-accelerated backend and that's ok.
  return defaultBackend;
}

mozilla::gfx::BackendType gfxWindowsPlatform::GetPreferredCanvasBackend() {
  mozilla::gfx::BackendType backend = gfxPlatform::GetPreferredCanvasBackend();

#ifdef MOZ_BUILD_WEBRENDER
  if (backend == BackendType::DIRECT2D1_1 && gfx::gfxVars::UseWebRender() &&
      !gfx::gfxVars::UseWebRenderANGLE()) {
    // We can't have D2D without ANGLE when WebRender is enabled, so fallback to
    // Skia.
    return BackendType::SKIA;
  }
#endif
  return backend;
}

gfxPlatformFontList* gfxWindowsPlatform::CreatePlatformFontList() {
  gfxPlatformFontList* pfl;

  // bug 630201 - older pre-RTM versions of Direct2D/DirectWrite cause odd
  // crashers so blacklist them altogether
  if (IsNotWin7PreRTM() && DWriteEnabled()) {
    pfl = new gfxDWriteFontList();
    if (NS_SUCCEEDED(pfl->InitFontList())) {
      return pfl;
    }
    // DWrite font initialization failed! Don't know why this would happen,
    // but apparently it can - see bug 594865.
    // So we're going to fall back to GDI fonts & rendering.
    gfxPlatformFontList::Shutdown();
    DisableD2D(FeatureStatus::Failed, "Failed to initialize fonts",
               "FEATURE_FAILURE_FONT_FAIL"_ns);
  }

  // Ensure this is false, even if the Windows version was recent enough to
  // permit it, as we're using GDI fonts.
  mHasVariationFontSupport = false;

  pfl = new gfxGDIFontList();

  if (NS_SUCCEEDED(pfl->InitFontList())) {
    return pfl;
  }

  gfxPlatformFontList::Shutdown();
  return nullptr;
}

// This function will permanently disable D2D for the session. It's intended to
// be used when, after initially chosing to use Direct2D, we encounter a
// scenario we can't support.
//
// This is called during gfxPlatform::Init() so at this point there should be no
// DrawTargetD2D/1 instances.
void gfxWindowsPlatform::DisableD2D(FeatureStatus aStatus, const char* aMessage,
                                    const nsACString& aFailureId) {
  gfxConfig::SetFailed(Feature::DIRECT2D, aStatus, aMessage, aFailureId);
  Factory::SetDirect3D11Device(nullptr);
  UpdateBackendPrefs();
}

already_AddRefed<gfxASurface> gfxWindowsPlatform::CreateOffscreenSurface(
    const IntSize& aSize, gfxImageFormat aFormat) {
  if (!Factory::AllowedSurfaceSize(aSize)) {
    return nullptr;
  }

  RefPtr<gfxASurface> surf = nullptr;

#ifdef CAIRO_HAS_WIN32_SURFACE
  if (mRenderMode == RENDER_GDI || mRenderMode == RENDER_DIRECT2D)
    surf = new gfxWindowsSurface(aSize, aFormat);
#endif

  if (!surf || surf->CairoStatus()) {
    surf = new gfxImageSurface(aSize, aFormat);
  }

  return surf.forget();
}

static const char kFontAparajita[] = "Aparajita";
static const char kFontArabicTypesetting[] = "Arabic Typesetting";
static const char kFontArial[] = "Arial";
static const char kFontArialUnicodeMS[] = "Arial Unicode MS";
static const char kFontCambria[] = "Cambria";
static const char kFontCambriaMath[] = "Cambria Math";
static const char kFontEbrima[] = "Ebrima";
static const char kFontEstrangeloEdessa[] = "Estrangelo Edessa";
static const char kFontEuphemia[] = "Euphemia";
static const char kFontGabriola[] = "Gabriola";
static const char kFontJavaneseText[] = "Javanese Text";
static const char kFontKhmerUI[] = "Khmer UI";
static const char kFontLaoUI[] = "Lao UI";
static const char kFontLeelawadeeUI[] = "Leelawadee UI";
static const char kFontLucidaSansUnicode[] = "Lucida Sans Unicode";
static const char kFontMVBoli[] = "MV Boli";
static const char kFontMalgunGothic[] = "Malgun Gothic";
static const char kFontMicrosoftJhengHei[] = "Microsoft JhengHei";
static const char kFontMicrosoftNewTaiLue[] = "Microsoft New Tai Lue";
static const char kFontMicrosoftPhagsPa[] = "Microsoft PhagsPa";
static const char kFontMicrosoftTaiLe[] = "Microsoft Tai Le";
static const char kFontMicrosoftUighur[] = "Microsoft Uighur";
static const char kFontMicrosoftYaHei[] = "Microsoft YaHei";
static const char kFontMicrosoftYiBaiti[] = "Microsoft Yi Baiti";
static const char kFontMeiryo[] = "Meiryo";
static const char kFontMongolianBaiti[] = "Mongolian Baiti";
static const char kFontMyanmarText[] = "Myanmar Text";
static const char kFontNirmalaUI[] = "Nirmala UI";
static const char kFontNyala[] = "Nyala";
static const char kFontPlantagenetCherokee[] = "Plantagenet Cherokee";
static const char kFontSegoeUI[] = "Segoe UI";
static const char kFontSegoeUIEmoji[] = "Segoe UI Emoji";
static const char kFontSegoeUISymbol[] = "Segoe UI Symbol";
static const char kFontSylfaen[] = "Sylfaen";
static const char kFontTraditionalArabic[] = "Traditional Arabic";
static const char kFontTwemojiMozilla[] = "Twemoji Mozilla";
static const char kFontUtsaah[] = "Utsaah";
static const char kFontYuGothic[] = "Yu Gothic";

void gfxWindowsPlatform::GetCommonFallbackFonts(
    uint32_t aCh, uint32_t aNextCh, Script aRunScript,
    nsTArray<const char*>& aFontList) {
  EmojiPresentation emoji = GetEmojiPresentation(aCh);
  if (emoji != EmojiPresentation::TextOnly) {
    if (aNextCh == kVariationSelector16 ||
        (aNextCh != kVariationSelector15 &&
         emoji == EmojiPresentation::EmojiDefault)) {
      // if char is followed by VS16, try for a color emoji glyph
      aFontList.AppendElement(kFontSegoeUIEmoji);
      aFontList.AppendElement(kFontTwemojiMozilla);
    }
  }

  // Arial is used as the default fallback for system fallback
  aFontList.AppendElement(kFontArial);

  if (!IS_IN_BMP(aCh)) {
    uint32_t p = aCh >> 16;
    if (p == 1) {  // SMP plane
      aFontList.AppendElement(kFontSegoeUISymbol);
      aFontList.AppendElement(kFontEbrima);
      aFontList.AppendElement(kFontNirmalaUI);
      aFontList.AppendElement(kFontCambriaMath);
    }
  } else {
    uint32_t b = (aCh >> 8) & 0xff;

    switch (b) {
      case 0x05:
        aFontList.AppendElement(kFontEstrangeloEdessa);
        aFontList.AppendElement(kFontCambria);
        break;
      case 0x06:
        aFontList.AppendElement(kFontMicrosoftUighur);
        break;
      case 0x07:
        aFontList.AppendElement(kFontEstrangeloEdessa);
        aFontList.AppendElement(kFontMVBoli);
        aFontList.AppendElement(kFontEbrima);
        break;
      case 0x09:
        aFontList.AppendElement(kFontNirmalaUI);
        aFontList.AppendElement(kFontUtsaah);
        aFontList.AppendElement(kFontAparajita);
        break;
      case 0x0a:
      case 0x0b:
      case 0x0c:
      case 0x0d:
        aFontList.AppendElement(kFontNirmalaUI);
        break;
      case 0x0e:
        aFontList.AppendElement(kFontLaoUI);
        aFontList.AppendElement(kFontLeelawadeeUI);
        break;
      case 0x10:
        aFontList.AppendElement(kFontMyanmarText);
        break;
      case 0x11:
        aFontList.AppendElement(kFontMalgunGothic);
        break;
      case 0x12:
      case 0x13:
        aFontList.AppendElement(kFontNyala);
        aFontList.AppendElement(kFontPlantagenetCherokee);
        break;
      case 0x14:
      case 0x15:
      case 0x16:
        aFontList.AppendElement(kFontEuphemia);
        aFontList.AppendElement(kFontSegoeUISymbol);
        break;
      case 0x17:
        aFontList.AppendElement(kFontKhmerUI);
        aFontList.AppendElement(kFontLeelawadeeUI);
        break;
      case 0x18:  // Mongolian
        aFontList.AppendElement(kFontMongolianBaiti);
        aFontList.AppendElement(kFontEuphemia);
        break;
      case 0x19:
        aFontList.AppendElement(kFontMicrosoftTaiLe);
        aFontList.AppendElement(kFontMicrosoftNewTaiLue);
        aFontList.AppendElement(kFontKhmerUI);
        aFontList.AppendElement(kFontLeelawadeeUI);
        break;
      case 0x1a:
        aFontList.AppendElement(kFontLeelawadeeUI);
        break;
      case 0x1c:
        aFontList.AppendElement(kFontNirmalaUI);
        break;
      case 0x20:  // Symbol ranges
      case 0x21:
      case 0x22:
      case 0x23:
      case 0x24:
      case 0x25:
      case 0x26:
      case 0x27:
      case 0x29:
      case 0x2a:
      case 0x2b:
      case 0x2c:
        aFontList.AppendElement(kFontSegoeUI);
        aFontList.AppendElement(kFontSegoeUISymbol);
        aFontList.AppendElement(kFontCambria);
        aFontList.AppendElement(kFontMeiryo);
        aFontList.AppendElement(kFontArial);
        aFontList.AppendElement(kFontLucidaSansUnicode);
        aFontList.AppendElement(kFontEbrima);
        break;
      case 0x2d:
      case 0x2e:
      case 0x2f:
        aFontList.AppendElement(kFontEbrima);
        aFontList.AppendElement(kFontNyala);
        aFontList.AppendElement(kFontSegoeUI);
        aFontList.AppendElement(kFontSegoeUISymbol);
        aFontList.AppendElement(kFontMeiryo);
        break;
      case 0x28:  // Braille
        aFontList.AppendElement(kFontSegoeUISymbol);
        break;
      case 0x30:
      case 0x31:
        aFontList.AppendElement(kFontMicrosoftYaHei);
        break;
      case 0x32:
        aFontList.AppendElement(kFontMalgunGothic);
        break;
      case 0x4d:
        aFontList.AppendElement(kFontSegoeUISymbol);
        break;
      case 0x9f:
        aFontList.AppendElement(kFontMicrosoftYaHei);
        aFontList.AppendElement(kFontYuGothic);
        break;
      case 0xa0:  // Yi
      case 0xa1:
      case 0xa2:
      case 0xa3:
      case 0xa4:
        aFontList.AppendElement(kFontMicrosoftYiBaiti);
        aFontList.AppendElement(kFontSegoeUI);
        break;
      case 0xa5:
      case 0xa6:
      case 0xa7:
        aFontList.AppendElement(kFontEbrima);
        aFontList.AppendElement(kFontSegoeUI);
        aFontList.AppendElement(kFontCambriaMath);
        break;
      case 0xa8:
        aFontList.AppendElement(kFontMicrosoftPhagsPa);
        aFontList.AppendElement(kFontNirmalaUI);
        break;
      case 0xa9:
        aFontList.AppendElement(kFontMalgunGothic);
        aFontList.AppendElement(kFontJavaneseText);
        aFontList.AppendElement(kFontLeelawadeeUI);
        break;
      case 0xaa:
        aFontList.AppendElement(kFontMyanmarText);
        break;
      case 0xab:
        aFontList.AppendElement(kFontEbrima);
        aFontList.AppendElement(kFontNyala);
        break;
      case 0xd7:
        aFontList.AppendElement(kFontMalgunGothic);
        break;
      case 0xfb:
        aFontList.AppendElement(kFontMicrosoftUighur);
        aFontList.AppendElement(kFontGabriola);
        aFontList.AppendElement(kFontSylfaen);
        break;
      case 0xfc:
      case 0xfd:
        aFontList.AppendElement(kFontTraditionalArabic);
        aFontList.AppendElement(kFontArabicTypesetting);
        break;
      case 0xfe:
        aFontList.AppendElement(kFontTraditionalArabic);
        aFontList.AppendElement(kFontMicrosoftJhengHei);
        break;
      case 0xff:
        aFontList.AppendElement(kFontMicrosoftJhengHei);
        break;
      default:
        break;
    }
  }

  // Arial Unicode MS has lots of glyphs for obscure characters,
  // use it as a last resort
  aFontList.AppendElement(kFontArialUnicodeMS);
}

gfxFontGroup* gfxWindowsPlatform::CreateFontGroup(
    const FontFamilyList& aFontFamilyList, const gfxFontStyle* aStyle,
    gfxTextPerfMetrics* aTextPerf, gfxUserFontSet* aUserFontSet,
    gfxFloat aDevToCssSize) {
  return new gfxFontGroup(aFontFamilyList, aStyle, aTextPerf, aUserFontSet,
                          aDevToCssSize);
}

bool gfxWindowsPlatform::DidRenderingDeviceReset(
    DeviceResetReason* aResetReason) {
  DeviceManagerDx* dm = DeviceManagerDx::Get();
  if (!dm) {
    return false;
  }
  return dm->HasDeviceReset(aResetReason);
}

void gfxWindowsPlatform::CompositorUpdated() {
  DeviceManagerDx::Get()->ForceDeviceReset(
      ForcedDeviceResetReason::COMPOSITOR_UPDATED);
  UpdateRenderMode();
}

BOOL CALLBACK InvalidateWindowForDeviceReset(HWND aWnd, LPARAM aMsg) {
  RedrawWindow(aWnd, nullptr, nullptr,
               RDW_INVALIDATE | RDW_INTERNALPAINT | RDW_FRAME);
  return TRUE;
}

void gfxWindowsPlatform::SchedulePaintIfDeviceReset() {
  AUTO_PROFILER_LABEL("gfxWindowsPlatform::SchedulePaintIfDeviceReset", OTHER);

  DeviceResetReason resetReason = DeviceResetReason::OK;
  if (!DidRenderingDeviceReset(&resetReason)) {
    return;
  }

  gfxCriticalNote << "(gfxWindowsPlatform) Detected device reset: "
                  << (int)resetReason;

  if (XRE_IsParentProcess()) {
    // Trigger an ::OnPaint for each window.
    ::EnumThreadWindows(GetCurrentThreadId(), InvalidateWindowForDeviceReset,
                        0);
  } else {
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "gfx::gfxWindowsPlatform::SchedulePaintIfDeviceReset", []() -> void {
          gfxWindowsPlatform::GetPlatform()->CheckForContentOnlyDeviceReset();
        }));
  }

  gfxCriticalNote << "(gfxWindowsPlatform) scheduled device update.";
}

void gfxWindowsPlatform::CheckForContentOnlyDeviceReset() {
  if (!DidRenderingDeviceReset()) {
    return;
  }

  bool isContentOnlyTDR;
  D3D11DeviceStatus status;

  DeviceManagerDx::Get()->ExportDeviceInfo(&status);
  CompositorBridgeChild::Get()->SendCheckContentOnlyTDR(status.sequenceNumber(),
                                                        &isContentOnlyTDR);

  // The parent process doesn't know about the reset yet, or the reset is
  // local to our device.
  if (isContentOnlyTDR) {
    gfxCriticalNote << "A content-only TDR is detected.";
    dom::ContentChild* cc = dom::ContentChild::GetSingleton();
    cc->RecvReinitRenderingForDeviceReset();
  }
}

nsTArray<uint8_t> gfxWindowsPlatform::GetPlatformCMSOutputProfileData() {
  HDC dc = ::GetDC(nullptr);
  if (!dc) {
    return nsTArray<uint8_t>();
  }

  WCHAR profilePath[MAX_PATH];
  DWORD profilePathLen = MAX_PATH;

  bool getProfileResult = ::GetICMProfileW(dc, &profilePathLen, profilePath);

  ::ReleaseDC(nullptr, dc);

  if (!getProfileResult) {
    return nsTArray<uint8_t>();
  }

  void* mem = nullptr;
  size_t size = 0;

  qcms_data_from_unicode_path(profilePath, &mem, &size);
  if (!mem) {
    return nsTArray<uint8_t>();
  }

  nsTArray<uint8_t> result;
  result.AppendElements(static_cast<uint8_t*>(mem), size);

  free(mem);

  return result;
}

bool gfxWindowsPlatform::UseClearTypeForDownloadableFonts() {
  if (mUseClearTypeForDownloadableFonts == UNINITIALIZED_VALUE) {
    mUseClearTypeForDownloadableFonts =
        Preferences::GetBool(GFX_DOWNLOADABLE_FONTS_USE_CLEARTYPE, true);
  }

  return mUseClearTypeForDownloadableFonts;
}

bool gfxWindowsPlatform::UseClearTypeAlways() {
  if (mUseClearTypeAlways == UNINITIALIZED_VALUE) {
    mUseClearTypeAlways = Preferences::GetBool(GFX_USE_CLEARTYPE_ALWAYS, false);
  }

  return mUseClearTypeAlways;
}

void gfxWindowsPlatform::GetDLLVersion(char16ptr_t aDLLPath,
                                       nsAString& aVersion) {
  DWORD versInfoSize, vers[4] = {0};
  // version info not available case
  aVersion.AssignLiteral(u"0.0.0.0");
  versInfoSize = GetFileVersionInfoSizeW(aDLLPath, nullptr);
  AutoTArray<BYTE, 512> versionInfo;

  if (versInfoSize == 0) {
    return;
  }

  // XXX(Bug 1631371) Check if this should use a fallible operation as it
  // pretended earlier.
  versionInfo.AppendElements(uint32_t(versInfoSize));

  if (!GetFileVersionInfoW(aDLLPath, 0, versInfoSize,
                           LPBYTE(versionInfo.Elements()))) {
    return;
  }

  UINT len = 0;
  VS_FIXEDFILEINFO* fileInfo = nullptr;
  if (!VerQueryValue(LPBYTE(versionInfo.Elements()), TEXT("\\"),
                     (LPVOID*)&fileInfo, &len) ||
      len == 0 || fileInfo == nullptr) {
    return;
  }

  DWORD fileVersMS = fileInfo->dwFileVersionMS;
  DWORD fileVersLS = fileInfo->dwFileVersionLS;

  vers[0] = HIWORD(fileVersMS);
  vers[1] = LOWORD(fileVersMS);
  vers[2] = HIWORD(fileVersLS);
  vers[3] = LOWORD(fileVersLS);

  char buf[256];
  SprintfLiteral(buf, "%u.%u.%u.%u", vers[0], vers[1], vers[2], vers[3]);
  aVersion.Assign(NS_ConvertUTF8toUTF16(buf));
}

static BOOL CALLBACK AppendClearTypeParams(HMONITOR aMonitor, HDC, LPRECT,
                                           LPARAM aContext) {
  MONITORINFOEXW monitorInfo;
  monitorInfo.cbSize = sizeof(MONITORINFOEXW);
  if (!GetMonitorInfoW(aMonitor, &monitorInfo)) {
    return TRUE;
  }

  ClearTypeParameterInfo ctinfo;
  ctinfo.displayName.Assign(monitorInfo.szDevice);

  RefPtr<IDWriteRenderingParams> renderingParams;
  HRESULT hr = Factory::GetDWriteFactory()->CreateMonitorRenderingParams(
      aMonitor, getter_AddRefs(renderingParams));
  if (FAILED(hr)) {
    return TRUE;
  }

  ctinfo.gamma = renderingParams->GetGamma() * 1000;
  ctinfo.pixelStructure = renderingParams->GetPixelGeometry();
  ctinfo.clearTypeLevel = renderingParams->GetClearTypeLevel() * 100;
  ctinfo.enhancedContrast = renderingParams->GetEnhancedContrast() * 100;

  auto* params = reinterpret_cast<nsTArray<ClearTypeParameterInfo>*>(aContext);
  params->AppendElement(ctinfo);
  return TRUE;
}

void gfxWindowsPlatform::GetCleartypeParams(
    nsTArray<ClearTypeParameterInfo>& aParams) {
  aParams.Clear();
  if (!DWriteEnabled()) {
    return;
  }
  EnumDisplayMonitors(nullptr, nullptr, AppendClearTypeParams,
                      reinterpret_cast<LPARAM>(&aParams));
}

void gfxWindowsPlatform::FontsPrefsChanged(const char* aPref) {
  bool clearTextFontCaches = true;

  gfxPlatform::FontsPrefsChanged(aPref);

  if (!aPref) {
    mUseClearTypeForDownloadableFonts = UNINITIALIZED_VALUE;
    mUseClearTypeAlways = UNINITIALIZED_VALUE;
  } else if (!strcmp(GFX_DOWNLOADABLE_FONTS_USE_CLEARTYPE, aPref)) {
    mUseClearTypeForDownloadableFonts = UNINITIALIZED_VALUE;
  } else if (!strcmp(GFX_USE_CLEARTYPE_ALWAYS, aPref)) {
    mUseClearTypeAlways = UNINITIALIZED_VALUE;
  } else if (!strncmp(GFX_CLEARTYPE_PARAMS, aPref,
                      strlen(GFX_CLEARTYPE_PARAMS))) {
    SetupClearTypeParams();
  } else {
    clearTextFontCaches = false;
  }

  if (clearTextFontCaches) {
    gfxFontCache* fc = gfxFontCache::GetCache();
    if (fc) {
      fc->Flush();
    }
  }
}

#define DISPLAY1_REGISTRY_KEY \
  HKEY_CURRENT_USER, L"Software\\Microsoft\\Avalon.Graphics\\DISPLAY1"

#define ENHANCED_CONTRAST_VALUE_NAME L"EnhancedContrastLevel"

void gfxWindowsPlatform::SetupClearTypeParams() {
  if (DWriteEnabled()) {
    // any missing prefs will default to invalid (-1) and be ignored;
    // out-of-range values will also be ignored
    FLOAT gamma = -1.0;
    FLOAT contrast = -1.0;
    FLOAT level = -1.0;
    int geometry = -1;
    int mode = -1;
    int32_t value;
    if (NS_SUCCEEDED(Preferences::GetInt(GFX_CLEARTYPE_PARAMS_GAMMA, &value))) {
      if (value >= 1000 && value <= 2200) {
        gamma = FLOAT(value / 1000.0);
      }
    }

    if (NS_SUCCEEDED(
            Preferences::GetInt(GFX_CLEARTYPE_PARAMS_CONTRAST, &value))) {
      if (value >= 0 && value <= 1000) {
        contrast = FLOAT(value / 100.0);
      }
    }

    if (NS_SUCCEEDED(Preferences::GetInt(GFX_CLEARTYPE_PARAMS_LEVEL, &value))) {
      if (value >= 0 && value <= 100) {
        level = FLOAT(value / 100.0);
      }
    }

    if (NS_SUCCEEDED(
            Preferences::GetInt(GFX_CLEARTYPE_PARAMS_STRUCTURE, &value))) {
      if (value >= 0 && value <= 2) {
        geometry = value;
      }
    }

    if (NS_SUCCEEDED(Preferences::GetInt(GFX_CLEARTYPE_PARAMS_MODE, &value))) {
      if (value >= 0 && value <= 5) {
        mode = value;
      }
    }

    cairo_dwrite_set_cleartype_params(gamma, contrast, level, geometry, mode);

    switch (mode) {
      case DWRITE_RENDERING_MODE_ALIASED:
      case DWRITE_RENDERING_MODE_CLEARTYPE_GDI_CLASSIC:
        mMeasuringMode = DWRITE_MEASURING_MODE_GDI_CLASSIC;
        break;
      case DWRITE_RENDERING_MODE_CLEARTYPE_GDI_NATURAL:
        mMeasuringMode = DWRITE_MEASURING_MODE_GDI_NATURAL;
        break;
      default:
        mMeasuringMode = DWRITE_MEASURING_MODE_NATURAL;
        break;
    }

    RefPtr<IDWriteRenderingParams> defaultRenderingParams;
    Factory::GetDWriteFactory()->CreateRenderingParams(
        getter_AddRefs(defaultRenderingParams));
    // For EnhancedContrast, we override the default if the user has not set it
    // in the registry (by using the ClearType Tuner).
    if (contrast < 0.0 || contrast > 10.0) {
      HKEY hKey;
      LONG res = RegOpenKeyExW(DISPLAY1_REGISTRY_KEY, 0, KEY_READ, &hKey);
      if (res == ERROR_SUCCESS) {
        res = RegQueryValueExW(hKey, ENHANCED_CONTRAST_VALUE_NAME, nullptr,
                               nullptr, nullptr, nullptr);
        if (res == ERROR_SUCCESS) {
          contrast = defaultRenderingParams->GetEnhancedContrast();
        }
        RegCloseKey(hKey);
      }

      if (contrast < 0.0 || contrast > 10.0) {
        contrast = 1.0;
      }
    }

    // For parameters that have not been explicitly set,
    // we copy values from default params (or our overridden value for contrast)
    if (gamma < 1.0 || gamma > 2.2) {
      gamma = defaultRenderingParams->GetGamma();
    }

    if (level < 0.0 || level > 1.0) {
      level = defaultRenderingParams->GetClearTypeLevel();
    }

    DWRITE_PIXEL_GEOMETRY dwriteGeometry =
        static_cast<DWRITE_PIXEL_GEOMETRY>(geometry);
    DWRITE_RENDERING_MODE renderMode = static_cast<DWRITE_RENDERING_MODE>(mode);

    if (dwriteGeometry < DWRITE_PIXEL_GEOMETRY_FLAT ||
        dwriteGeometry > DWRITE_PIXEL_GEOMETRY_BGR) {
      dwriteGeometry = defaultRenderingParams->GetPixelGeometry();
    }

    Factory::SetBGRSubpixelOrder(dwriteGeometry == DWRITE_PIXEL_GEOMETRY_BGR);

    if (renderMode < DWRITE_RENDERING_MODE_DEFAULT ||
        renderMode > DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL_SYMMETRIC) {
      renderMode = defaultRenderingParams->GetRenderingMode();
    }

    mRenderingParams[TEXT_RENDERING_NO_CLEARTYPE] = defaultRenderingParams;

    HRESULT hr = Factory::GetDWriteFactory()->CreateCustomRenderingParams(
        gamma, contrast, level, dwriteGeometry, renderMode,
        getter_AddRefs(mRenderingParams[TEXT_RENDERING_NORMAL]));
    if (FAILED(hr) || !mRenderingParams[TEXT_RENDERING_NORMAL]) {
      mRenderingParams[TEXT_RENDERING_NORMAL] = defaultRenderingParams;
    }

    hr = Factory::GetDWriteFactory()->CreateCustomRenderingParams(
        gamma, contrast, level, dwriteGeometry,
        DWRITE_RENDERING_MODE_CLEARTYPE_GDI_CLASSIC,
        getter_AddRefs(mRenderingParams[TEXT_RENDERING_GDI_CLASSIC]));
    if (FAILED(hr) || !mRenderingParams[TEXT_RENDERING_GDI_CLASSIC]) {
      mRenderingParams[TEXT_RENDERING_GDI_CLASSIC] = defaultRenderingParams;
    }
  }
}

ReadbackManagerD3D11* gfxWindowsPlatform::GetReadbackManager() {
  if (!mD3D11ReadbackManager) {
    mD3D11ReadbackManager = new ReadbackManagerD3D11();
  }

  return mD3D11ReadbackManager;
}

bool gfxWindowsPlatform::IsOptimus() {
  static int knowIsOptimus = -1;
  if (knowIsOptimus == -1) {
    // other potential optimus -- nvd3d9wrapx.dll & nvdxgiwrap.dll
    if (GetModuleHandleA("nvumdshim.dll") ||
        GetModuleHandleA("nvumdshimx.dll")) {
      knowIsOptimus = 1;
    } else {
      knowIsOptimus = 0;
    }
  }
  return knowIsOptimus;
}
/*
static inline bool
IsWARPStable()
{
  // It seems like nvdxgiwrap makes a mess of WARP. See bug 1154703.
  if (!IsWin8OrLater() || GetModuleHandleA("nvdxgiwrap.dll")) {
    return false;
  }
  return true;
}
*/
static void InitializeANGLEConfig() {
  FeatureState& d3d11ANGLE = gfxConfig::GetFeature(Feature::D3D11_HW_ANGLE);

  if (!gfxConfig::IsEnabled(Feature::D3D11_COMPOSITING)) {
    d3d11ANGLE.DisableByDefault(FeatureStatus::Unavailable,
                                "D3D11 compositing is disabled",
                                "FEATURE_FAILURE_D3D11_DISABLED"_ns);
    return;
  }

  d3d11ANGLE.EnableByDefault();

  nsCString message;
  nsCString failureId;
  if (!gfxPlatform::IsGfxInfoStatusOkay(nsIGfxInfo::FEATURE_DIRECT3D_11_ANGLE,
                                        &message, failureId)) {
    d3d11ANGLE.Disable(FeatureStatus::Blacklisted, message.get(), failureId);
  }
}

void gfxWindowsPlatform::InitializeDirectDrawConfig() {
  MOZ_ASSERT(XRE_IsParentProcess());

  FeatureState& ddraw = gfxConfig::GetFeature(Feature::DIRECT_DRAW);
  ddraw.EnableByDefault();
}

void gfxWindowsPlatform::InitializeConfig() {
  if (XRE_IsParentProcess()) {
    // The parent process first determines which features can be attempted.
    // This information is relayed to content processes and the GPU process.
    InitializeD3D9Config();
    InitializeD3D11Config();
    InitializeANGLEConfig();
    InitializeD2DConfig();
  } else {
    FetchAndImportContentDeviceData();
    InitializeANGLEConfig();
  }
}

void gfxWindowsPlatform::InitializeD3D9Config() {
  MOZ_ASSERT(XRE_IsParentProcess());

  FeatureState& d3d9 = gfxConfig::GetFeature(Feature::D3D9_COMPOSITING);

  if (!gfxConfig::IsEnabled(Feature::HW_COMPOSITING)) {
    d3d9.DisableByDefault(
        FeatureStatus::Unavailable, "Hardware compositing is disabled",
        NS_LITERAL_CSTRING("FEATURE_FAILURE_D3D9_NEED_HWCOMP"));
    return;
  }

  d3d9.SetDefaultFromPref(
      StaticPrefs::GetPrefName_layers_allow_d3d9_fallback(), true,
      StaticPrefs::GetPrefDefault_layers_allow_d3d9_fallback());

  if (!d3d9.IsEnabled() && StaticPrefs::layers_prefer_d3d9_AtStartup()) {
    d3d9.UserEnable("Direct3D9 enabled via layers.prefer-d3d9");
  }

  nsCString message;
  nsCString failureId;
  if (!gfxPlatform::IsGfxInfoStatusOkay(nsIGfxInfo::FEATURE_DIRECT3D_9_LAYERS,
                                        &message, failureId)) {
    d3d9.Disable(FeatureStatus::Blacklisted, message.get(), failureId);
  }

  if (gfxConfig::IsForcedOnByUser(Feature::HW_COMPOSITING)) {
    d3d9.UserForceEnable("Hardware compositing is force-enabled");
  }
}

void gfxWindowsPlatform::InitializeD3D11Config() {
  MOZ_ASSERT(XRE_IsParentProcess());

  FeatureState& d3d11 = gfxConfig::GetFeature(Feature::D3D11_COMPOSITING);

  if (!gfxConfig::IsEnabled(Feature::HW_COMPOSITING)) {
    d3d11.DisableByDefault(FeatureStatus::Unavailable,
                           "Hardware compositing is disabled",
                           "FEATURE_FAILURE_D3D11_NEED_HWCOMP"_ns);
    return;
  }

  d3d11.EnableByDefault();

  // If the user prefers D3D9, act as though they disabled D3D11.
  if (StaticPrefs::layers_prefer_d3d9_AtStartup()) {
    d3d11.UserDisable("Disabled due to user preference for Direct3D 9",
                      "FEATURE_FAILURE_D3D11_PREF"_ns);
    return;
  }

  // Check if the user really, really wants WARP.
  if (StaticPrefs::layers_d3d11_force_warp_AtStartup()) {
    // Force D3D11 on even if we disabled it.
    d3d11.UserForceEnable("User force-enabled WARP");
  }

  if (!IsWin8OrLater() &&
      !DeviceManagerDx::Get()->CheckRemotePresentSupport()) {
    nsCOMPtr<nsIGfxInfo> gfxInfo;
    gfxInfo = services::GetGfxInfo();
    nsAutoString adaptorId;
    gfxInfo->GetAdapterDeviceID(adaptorId);
    // Blacklist Intel HD Graphics 510/520/530 on Windows 7 without platform
    // update due to the crashes in Bug 1351349.
    if (adaptorId.EqualsLiteral("0x1912") ||
        adaptorId.EqualsLiteral("0x1916") ||
        adaptorId.EqualsLiteral("0x1902")) {
#ifdef RELEASE_OR_BETA
      d3d11.Disable(FeatureStatus::Blacklisted, "Blacklisted, see bug 1351349",
                    "FEATURE_FAILURE_BUG_1351349"_ns);
#else
      Preferences::SetBool("gfx.compositor.clearstate", true);
#endif
    }
  }

  nsCString message;
  nsCString failureId;
  if (StaticPrefs::layers_d3d11_enable_blacklist_AtStartup() &&
      !gfxPlatform::IsGfxInfoStatusOkay(nsIGfxInfo::FEATURE_DIRECT3D_11_LAYERS,
                                        &message, failureId)) {
    d3d11.Disable(FeatureStatus::Blacklisted, message.get(), failureId);
  }

  InitializeAdvancedLayersConfig();
}

/* static */
void gfxWindowsPlatform::InitializeAdvancedLayersConfig() {
  // Only enable Advanced Layers if D3D11 succeeded.
  if (!gfxConfig::IsEnabled(Feature::D3D11_COMPOSITING) ||
      !IsVistaSP1OrLater()) {
    return;
  }

  FeatureState& al = gfxConfig::GetFeature(Feature::ADVANCED_LAYERS);
  al.SetDefaultFromPref(StaticPrefs::GetPrefName_layers_mlgpu_enabled(),
                        true /* aIsEnablePref */,
                        StaticPrefs::GetPrefDefault_layers_mlgpu_enabled());

  // Windows 7 has an extra pref since it uses totally different buffer paths
  // that haven't been performance tested yet.
  if (al.IsEnabled() && !IsWin8OrLater()) {
    if (StaticPrefs::layers_mlgpu_enable_on_windows7_AtStartup()) {
      al.UserEnable("Enabled for Windows 7 via user-preference");
    } else {
      al.Disable(FeatureStatus::Disabled,
                 "Advanced Layers is disabled on Windows 7 by default",
                 "FEATURE_FAILURE_DISABLED_ON_WIN7"_ns);
    }
  }

  nsCString message, failureId;
  if (!IsGfxInfoStatusOkay(nsIGfxInfo::FEATURE_ADVANCED_LAYERS, &message,
                           failureId)) {
    al.Disable(FeatureStatus::Blacklisted, message.get(), failureId);
  } else if (Preferences::GetBool("layers.mlgpu.sanity-test-failed", false)) {
    al.Disable(FeatureStatus::Broken, "Failed to render sanity test",
               "FEATURE_FAILURE_FAILED_TO_RENDER"_ns);
  }
}

// Supports lazy device initialization on Windows, so that WebRender can avoid
// initializing GPU state and allocating swap chains for most non-GPU processes.
void gfxWindowsPlatform::EnsureDevicesInitialized() {
  if (!mInitializedDevices) {
    mInitializedDevices = true;
    InitializeDevices();
    UpdateBackendPrefs();
  }
}

bool gfxWindowsPlatform::DevicesInitialized() { return mInitializedDevices; }

void gfxWindowsPlatform::InitializeDevices() {
  MOZ_ASSERT(NS_IsMainThread());

  if (XRE_IsParentProcess()) {
    // If we're the UI process, and the GPU process is enabled, then we don't
    // initialize any DirectX devices. We do leave them enabled in gfxConfig
    // though. If the GPU process fails to create these devices it will send
    // a message back and we'll update their status.
    if (InitGPUProcessSupport()) {
      return;
    }

    // No GPU process, continue initializing devices as normal.
  }

  // If acceleration is disabled, we refuse to initialize anything.
  if (!gfxConfig::IsEnabled(Feature::HW_COMPOSITING) || !IsVistaSP1OrLater()) {
    return;
  }

  // If we previously crashed initializing devices, bail out now.
  D3D11LayersCrashGuard detectCrashes;
  if (detectCrashes.Crashed()) {
    gfxConfig::SetFailed(Feature::HW_COMPOSITING,
                         FeatureStatus::CrashedOnStartup,
                         "Crashed during startup in a previous session");
    gfxConfig::SetFailed(
        Feature::D3D11_COMPOSITING, FeatureStatus::CrashedOnStartup,
        "Harware acceleration crashed during startup in a previous session");
    gfxConfig::SetFailed(
        Feature::DIRECT2D, FeatureStatus::CrashedOnStartup,
        "Harware acceleration crashed during startup in a previous session");
    return;
  }

  // First, initialize D3D11. If this succeeds we attempt to use Direct2D.
  InitializeD3D11();
  InitializeD2D();
}

void gfxWindowsPlatform::InitializeD3D11() {
  // This function attempts to initialize our D3D11 devices, if the hardware
  // is not blacklisted for D3D11 layers. This first attempt will try to create
  // a hardware accelerated device. If this creation fails or the hardware is
  // blacklisted, then this function will abort if WARP is disabled, causing us
  // to fallback to D3D9 or Basic layers. If WARP is not disabled it will use
  // a WARP device which should always be available on Windows 7 and higher.
  if (!gfxConfig::IsEnabled(Feature::D3D11_COMPOSITING)) {
    return;
  }

  DeviceManagerDx* dm = DeviceManagerDx::Get();
  if (XRE_IsParentProcess()) {
    if (!dm->CreateCompositorDevices()) {
      return;
    }
  }

  dm->CreateContentDevices();

  // Content process failed to create the d3d11 device while parent process
  // succeed.
  if (XRE_IsContentProcess() &&
      !gfxConfig::IsEnabled(Feature::D3D11_COMPOSITING)) {
    gfxCriticalError()
        << "[D3D11] Failed to create the D3D11 device in content \
                           process.";
  }
}

void gfxWindowsPlatform::InitializeD2DConfig() {
  FeatureState& d2d1 = gfxConfig::GetFeature(Feature::DIRECT2D);

  if (!IsVistaOrLater()) {
    d2d1.DisableByDefault(FeatureStatus::Unavailable,
                          "Direct2D is not available on Windows XP",
                          "FEATURE_FAILURE_D2D_XP"_ns);
    return;
  }
  if (!gfxConfig::IsEnabled(Feature::D3D11_COMPOSITING)) {
    d2d1.DisableByDefault(FeatureStatus::Unavailable,
                          "Direct2D requires Direct3D 11 compositing",
                          "FEATURE_FAILURE_D2D_D3D11_COMP"_ns);
    return;
  }

  d2d1.SetDefaultFromPref(StaticPrefs::GetPrefName_gfx_direct2d_disabled(),
                          false,
                          StaticPrefs::GetPrefDefault_gfx_direct2d_disabled());

  nsCString message;
  nsCString failureId;
  if (!gfxPlatform::IsGfxInfoStatusOkay(nsIGfxInfo::FEATURE_DIRECT2D, &message,
                                        failureId)) {
    d2d1.Disable(FeatureStatus::Blacklisted, message.get(), failureId);
  }

  if (!d2d1.IsEnabled() &&
      StaticPrefs::gfx_direct2d_force_enabled_AtStartup()) {
    d2d1.UserForceEnable("Force-enabled via user-preference");
  }
}

void gfxWindowsPlatform::InitializeD2D() {
  ScopedGfxFeatureReporter d2d1_1("D2D1.1");

  FeatureState& d2d1 = gfxConfig::GetFeature(Feature::DIRECT2D);

  DeviceManagerDx* dm = DeviceManagerDx::Get();

  // We don't know this value ahead of time, but the user can force-override
  // it, so we use Disable instead of SetFailed.
  if (dm->IsWARP()) {
    d2d1.Disable(FeatureStatus::Blocked,
                 "Direct2D is not compatible with Direct3D11 WARP",
                 "FEATURE_FAILURE_D2D_WARP_BLOCK"_ns);
  }

  // If we pass all the initial checks, we can proceed to runtime decisions.
  if (!d2d1.IsEnabled()) {
    return;
  }

  if (!Factory::SupportsD2D1()) {
    d2d1.SetFailed(FeatureStatus::Unavailable,
                   "Failed to acquire a Direct2D 1.1 factory",
                   "FEATURE_FAILURE_D2D_FACTORY"_ns);
    return;
  }

  if (!dm->GetContentDevice()) {
    d2d1.SetFailed(FeatureStatus::Failed,
                   "Failed to acquire a Direct3D 11 content device",
                   "FEATURE_FAILURE_D2D_DEVICE"_ns);
    return;
  }

  if (!dm->TextureSharingWorks()) {
    d2d1.SetFailed(FeatureStatus::Failed,
                   "Direct3D11 device does not support texture sharing",
                   "FEATURE_FAILURE_D2D_TXT_SHARING"_ns);
    return;
  }

  // Using Direct2D depends on DWrite support.
  if (!DWriteEnabled() && !InitDWriteSupport()) {
    d2d1.SetFailed(FeatureStatus::Failed,
                   "Failed to initialize DirectWrite support",
                   "FEATURE_FAILURE_D2D_DWRITE"_ns);
    return;
  }

  // Verify that Direct2D device creation succeeded.
  RefPtr<ID3D11Device> contentDevice = dm->GetContentDevice();
  if (!Factory::SetDirect3D11Device(contentDevice)) {
    d2d1.SetFailed(FeatureStatus::Failed, "Failed to create a Direct2D device",
                   "FEATURE_FAILURE_D2D_CREATE_FAILED"_ns);
    return;
  }

  MOZ_ASSERT(d2d1.IsEnabled());
  d2d1_1.SetSuccessful();
}

bool gfxWindowsPlatform::InitGPUProcessSupport() {
  FeatureState& gpuProc = gfxConfig::GetFeature(Feature::GPU_PROCESS);

  if (!gpuProc.IsEnabled()) {
    return false;
  }

  nsCString message;
  nsCString failureId;
  if (!gfxPlatform::IsGfxInfoStatusOkay(nsIGfxInfo::FEATURE_GPU_PROCESS,
                                        &message, failureId)) {
    gpuProc.Disable(FeatureStatus::Blacklisted, message.get(), failureId);
    return false;
  }

  if (!IsWin7SP1OrLater()) {
    // For Windows XP, we simply don't care enough to support this
    // configuration. On Windows Vista and 7 Pre-SP1, DXGI 1.2 is not
    // available and remote presentation for D3D11 will not work. Rather
    // than take a regression and use D3D9, we revert back to in-process
    // rendering.
    gpuProc.Disable(
        FeatureStatus::Unavailable,
        "Windows XP, Vista, and 7 Pre-SP1 cannot use the GPU process",
        "FEATURE_FAILURE_OLD_WINDOWS"_ns);
  } else if (!IsWin8OrLater()) {
    // Windows 7 SP1 can have DXGI 1.2 only via the Platform Update, so we
    // explicitly check for that here.
    if (!DeviceManagerDx::Get()->CheckRemotePresentSupport()) {
      gpuProc.Disable(FeatureStatus::Unavailable,
                      "GPU Process requires the Windows 7 Platform Update",
                      "FEATURE_FAILURE_PLATFORM_UPDATE"_ns);
    } else {
      // Clear anything cached by the above call since we don't need it.
      DeviceManagerDx::Get()->ResetDevices();
    }
  } else if (!gfxConfig::IsEnabled(Feature::D3D11_COMPOSITING)) {
    // Don't use the GPU process if not using D3D11, unless software
    // compositor is allowed
    if (StaticPrefs::layers_gpu_process_allow_software_AtStartup()) {
      return gpuProc.IsEnabled();
    }
    gpuProc.Disable(FeatureStatus::Unavailable,
                    "Not using GPU Process since D3D11 is unavailable",
                    "FEATURE_FAILURE_NO_D3D11"_ns);
  }
  // If we're still enabled at this point, the user set the force-enabled pref.
  return gpuProc.IsEnabled();
}

bool gfxWindowsPlatform::DwmCompositionEnabled() {
  if (!IsVistaOrLater()) {
    return false;
  }

  MOZ_ASSERT(WinUtils::dwmIsCompositionEnabledPtr);
  BOOL dwmEnabled = false;

  if (FAILED(WinUtils::dwmIsCompositionEnabledPtr(&dwmEnabled))) {
    return false;
  }

  return dwmEnabled;
}

class D3DVsyncSource final : public VsyncSource {
 public:
  class D3DVsyncDisplay final : public VsyncSource::Display {
    NS_INLINE_DECL_THREADSAFE_REFCOUNTING(D3DVsyncDisplay)
   public:
    D3DVsyncDisplay()
        : mPrevVsync(TimeStamp::Now()),
          mVsyncEnabledLock("D3DVsyncEnabledLock"),
          mVsyncEnabled(false) {
      mVsyncThread = new base::Thread("WindowsVsyncThread");
      MOZ_RELEASE_ASSERT(mVsyncThread->Start(),
                         "GFX: Could not start Windows vsync thread");
      SetVsyncRate();
    }

    void SetVsyncRate() {
      if (!gfxWindowsPlatform::GetPlatform()->DwmCompositionEnabled()) {
        mVsyncRate = TimeDuration::FromMilliseconds(1000.0 / 60.0);
        return;
      }

      DWM_TIMING_INFO vblankTime;
      // Make sure to init the cbSize, otherwise GetCompositionTiming will fail
      vblankTime.cbSize = sizeof(DWM_TIMING_INFO);
      HRESULT hr = WinUtils::dwmGetCompositionTimingInfoPtr(0, &vblankTime);
      if (SUCCEEDED(hr)) {
        UNSIGNED_RATIO refreshRate = vblankTime.rateRefresh;
        // We get the rate in hertz / time, but we want the rate in ms.
        float rate = ((float)refreshRate.uiDenominator /
                      (float)refreshRate.uiNumerator) *
                     1000;
        mVsyncRate = TimeDuration::FromMilliseconds(rate);
      } else {
        mVsyncRate = TimeDuration::FromMilliseconds(1000.0 / 60.0);
      }
    }

    virtual void Shutdown() override {
      MOZ_ASSERT(NS_IsMainThread());
      DisableVsync();
      mVsyncThread->Stop();
      delete mVsyncThread;
    }

    virtual void EnableVsync() override {
      MOZ_ASSERT(NS_IsMainThread());
      MOZ_ASSERT(mVsyncThread->IsRunning());
      {  // scope lock
        Monitor2AutoLock lock(mVsyncEnabledLock);
        if (mVsyncEnabled) {
          return;
        }
        mVsyncEnabled = true;
      }

      mVsyncThread->message_loop()->PostTask(NewRunnableMethod(
          "D3DVsyncDisplay::VBlankLoop", this, &D3DVsyncDisplay::VBlankLoop));
    }

    virtual void DisableVsync() override {
      MOZ_ASSERT(NS_IsMainThread());
      MOZ_ASSERT(mVsyncThread->IsRunning());
      Monitor2AutoLock lock(mVsyncEnabledLock);
      if (!mVsyncEnabled) {
        return;
      }
      mVsyncEnabled = false;
    }

    virtual bool IsVsyncEnabled() override {
      MOZ_ASSERT(NS_IsMainThread());
      Monitor2AutoLock lock(mVsyncEnabledLock);
      return mVsyncEnabled;
    }

    virtual TimeDuration GetVsyncRate() override { return mVsyncRate; }

    void ScheduleSoftwareVsync(TimeStamp aVsyncTimestamp) {
      MOZ_ASSERT(IsInVsyncThread());
      NS_WARNING(
          "DwmComposition dynamically disabled, falling back to software "
          "timers");

      TimeStamp nextVsync = aVsyncTimestamp + mVsyncRate;
      TimeDuration delay = nextVsync - TimeStamp::Now();
      if (delay.ToMilliseconds() < 0) {
        delay = mozilla::TimeDuration::FromMilliseconds(0);
      }

      mVsyncThread->message_loop()->PostDelayedTask(
          NewRunnableMethod("D3DVsyncDisplay::VBlankLoop", this,
                            &D3DVsyncDisplay::VBlankLoop),
          delay.ToMilliseconds());
    }

    // Returns the timestamp for the just happened vsync
    TimeStamp GetVBlankTime() {
      TimeStamp vsync = TimeStamp::Now();
      TimeStamp now = vsync;

      DWM_TIMING_INFO vblankTime;
      // Make sure to init the cbSize, otherwise
      // GetCompositionTiming will fail
      vblankTime.cbSize = sizeof(DWM_TIMING_INFO);
      HRESULT hr = WinUtils::dwmGetCompositionTimingInfoPtr(0, &vblankTime);
      if (!SUCCEEDED(hr)) {
        return vsync;
      }

      LARGE_INTEGER frequency;
      QueryPerformanceFrequency(&frequency);

      LARGE_INTEGER qpcNow;
      QueryPerformanceCounter(&qpcNow);

      const int microseconds = 1000000;
      int64_t adjust = qpcNow.QuadPart - vblankTime.qpcVBlank;
      int64_t usAdjust = (adjust * microseconds) / frequency.QuadPart;
      vsync -= TimeDuration::FromMicroseconds((double)usAdjust);

      if (IsWin10OrLater()) {
        // On Windows 10 and on, DWMGetCompositionTimingInfo, mostly
        // reports the upcoming vsync time, which is in the future.
        // It can also sometimes report a vblank time in the past.
        // Since large parts of Gecko assume TimeStamps can't be in future,
        // use the previous vsync.

        // Windows 10 and Intel HD vsync timestamps are messy and
        // all over the place once in a while. Most of the time,
        // it reports the upcoming vsync. Sometimes, that upcoming
        // vsync is in the past. Sometimes that upcoming vsync is before
        // the previously seen vsync.
        // In these error cases, normalize to Now();
        if (vsync >= now) {
          vsync = vsync - mVsyncRate;
        }
      }

      // On Windows 7 and 8, DwmFlush wakes up AFTER qpcVBlankTime
      // from DWMGetCompositionTimingInfo. We can return the adjusted vsync.
      if (vsync >= now) {
        vsync = now;
      }

      // Our vsync time is some time very far in the past, adjust to Now.
      // 4 ms is arbitrary, so feel free to pick something else if this isn't
      // working. See the comment above within IsWin10OrLater().
      if ((now - vsync).ToMilliseconds() > 4.0) {
        vsync = now;
      }

      return vsync;
    }

    void VBlankLoop() {
      MOZ_ASSERT(IsInVsyncThread());
      MOZ_ASSERT(sizeof(int64_t) == sizeof(QPC_TIME));

      TimeStamp vsync = TimeStamp::Now();
      mPrevVsync = TimeStamp();
      TimeStamp flushTime = TimeStamp::Now();
      TimeDuration longVBlank = mVsyncRate * 2;

      for (;;) {
        {  // scope lock
          Monitor2AutoLock lock(mVsyncEnabledLock);
          if (!mVsyncEnabled) return;
        }

        // Large parts of gecko assume that the refresh driver timestamp
        // must be <= Now() and cannot be in the future.
        MOZ_ASSERT(vsync <= TimeStamp::Now());
        Display::NotifyVsync(vsync);

        // DwmComposition can be dynamically enabled/disabled
        // so we have to check every time that it's available.
        // When it is unavailable, we fallback to software but will try
        // to get back to dwm rendering once it's re-enabled
        if (!gfxWindowsPlatform::GetPlatform()->DwmCompositionEnabled()) {
          ScheduleSoftwareVsync(vsync);
          return;
        }

        // Using WaitForVBlank, the whole system dies because WaitForVBlank
        // only works if it's run on the same thread as the Present();
        HRESULT hr = WinUtils::dwmFlushProcPtr();
        if (!SUCCEEDED(hr)) {
          // DWMFlush isn't working, fallback to software vsync.
          ScheduleSoftwareVsync(TimeStamp::Now());
          return;
        }

        TimeStamp now = TimeStamp::Now();
        TimeDuration flushDiff = now - flushTime;
        flushTime = now;
        if ((flushDiff > longVBlank) || mPrevVsync.IsNull()) {
          // Our vblank took longer than 2 intervals, readjust our timestamps
          vsync = GetVBlankTime();
          mPrevVsync = vsync;
        } else {
          // Instead of giving the actual vsync time, a constant interval
          // between vblanks instead of the noise generated via hardware
          // is actually what we want. Most apps just care about the diff
          // between vblanks to animate, so a clean constant interval is
          // smoother.
          vsync = mPrevVsync + mVsyncRate;
          if (vsync > now) {
            // DWMFlush woke up very early, so readjust our times again
            vsync = GetVBlankTime();
          }

          if (vsync <= mPrevVsync) {
            vsync = TimeStamp::Now();
          }

          if ((now - vsync).ToMilliseconds() > 2.0) {
            // Account for time drift here where vsync never quite catches up to
            // Now and we'd fall ever so slightly further behind Now().
            vsync = GetVBlankTime();
          }

          mPrevVsync = vsync;
        }
      }  // end for
    }

   private:
    virtual ~D3DVsyncDisplay() { MOZ_ASSERT(NS_IsMainThread()); }

    bool IsInVsyncThread() {
      return mVsyncThread->thread_id() == PlatformThread::CurrentId();
    }

    TimeStamp mPrevVsync;
    Monitor2 mVsyncEnabledLock;
    base::Thread* mVsyncThread;
    TimeDuration mVsyncRate;
    bool mVsyncEnabled;
  };  // end d3dvsyncdisplay

  D3DVsyncSource() { mPrimaryDisplay = new D3DVsyncDisplay(); }

  virtual Display& GetGlobalDisplay() override { return *mPrimaryDisplay; }

 private:
  virtual ~D3DVsyncSource() = default;
  RefPtr<D3DVsyncDisplay> mPrimaryDisplay;
};  // end D3DVsyncSource

already_AddRefed<mozilla::gfx::VsyncSource>
gfxWindowsPlatform::CreateHardwareVsyncSource() {
  MOZ_RELEASE_ASSERT(NS_IsMainThread(), "GFX: Not in main thread.");

  if (!WinUtils::dwmIsCompositionEnabledPtr) {
    NS_WARNING("Dwm composition not available, falling back to software vsync");
    return gfxPlatform::CreateHardwareVsyncSource();
  }

  BOOL dwmEnabled = false;
  WinUtils::dwmIsCompositionEnabledPtr(&dwmEnabled);
  if (!dwmEnabled) {
    NS_WARNING("DWM not enabled, falling back to software vsync");
    return gfxPlatform::CreateHardwareVsyncSource();
  }

  RefPtr<VsyncSource> d3dVsyncSource = new D3DVsyncSource();
  return d3dVsyncSource.forget();
}

void gfxWindowsPlatform::GetAcceleratedCompositorBackends(
    nsTArray<LayersBackend>& aBackends) {
  if (gfxConfig::IsEnabled(Feature::OPENGL_COMPOSITING) &&
      StaticPrefs::layers_prefer_opengl_AtStartup()) {
    aBackends.AppendElement(LayersBackend::LAYERS_OPENGL);
  }

  if (IsVistaSP1OrLater()) {
    if (gfxConfig::IsEnabled(Feature::D3D9_COMPOSITING) &&
        StaticPrefs::layers_prefer_d3d9_AtStartup()) {
      aBackends.AppendElement(LayersBackend::LAYERS_D3D9);
    }

    if (gfxConfig::IsEnabled(Feature::D3D11_COMPOSITING)) {
      aBackends.AppendElement(LayersBackend::LAYERS_D3D11);
    }
  } else if (gfxConfig::IsEnabled(Feature::D3D9_COMPOSITING)) {
    aBackends.AppendElement(LayersBackend::LAYERS_D3D9);
  }
}

void gfxWindowsPlatform::ImportGPUDeviceData(
    const mozilla::gfx::GPUDeviceData& aData) {
  MOZ_ASSERT(XRE_IsParentProcess());

  gfxPlatform::ImportGPUDeviceData(aData);

  gfxConfig::ImportChange(Feature::D3D11_COMPOSITING, aData.d3d11Compositing());
  gfxConfig::ImportChange(Feature::D3D9_COMPOSITING, aData.d3d9Compositing());

  DeviceManagerDx* dm = DeviceManagerDx::Get();
  if (gfxConfig::IsEnabled(Feature::D3D11_COMPOSITING)) {
    dm->ImportDeviceInfo(aData.gpuDevice().ref());
  } else {
    // There should be no devices, so this just takes away the device status.
    dm->ResetDevices();

    // Make sure we disable D2D if content processes might use it.
    FeatureState& d2d1 = gfxConfig::GetFeature(Feature::DIRECT2D);
    if (d2d1.IsEnabled()) {
      d2d1.SetFailed(FeatureStatus::Unavailable,
                     "Direct2D requires Direct3D 11 compositing",
                     "FEATURE_FAILURE_D2D_D3D11_COMP"_ns);
    }
  }

  // CanUseHardwareVideoDecoding depends on d3d11 state, so update
  // the cached value now.
  UpdateCanUseHardwareVideoDecoding();

  // For completeness (and messaging in about:support). Content recomputes this
  // on its own, and we won't use ANGLE in the UI process if we're using a GPU
  // process.
  UpdateANGLEConfig();
}

void gfxWindowsPlatform::ImportContentDeviceData(
    const mozilla::gfx::ContentDeviceData& aData) {
  MOZ_ASSERT(XRE_IsContentProcess());

  gfxPlatform::ImportContentDeviceData(aData);

  const DevicePrefs& prefs = aData.prefs();
  gfxConfig::Inherit(Feature::D3D11_COMPOSITING, prefs.d3d11Compositing());
  gfxConfig::Inherit(Feature::D3D9_COMPOSITING, prefs.d3d9Compositing());
  gfxConfig::Inherit(Feature::DIRECT2D, prefs.useD2D1());

  if (gfxConfig::IsEnabled(Feature::D3D11_COMPOSITING)) {
    DeviceManagerDx* dm = DeviceManagerDx::Get();
    dm->ImportDeviceInfo(aData.d3d11());
  }
}

void gfxWindowsPlatform::BuildContentDeviceData(ContentDeviceData* aOut) {
  // Check for device resets before giving back new graphics information.
  UpdateRenderMode();

  gfxPlatform::BuildContentDeviceData(aOut);

  const FeatureState& d3d11 = gfxConfig::GetFeature(Feature::D3D11_COMPOSITING);
  aOut->prefs().d3d11Compositing() = d3d11.GetValue();
  aOut->prefs().d3d9Compositing() =
      gfxConfig::GetValue(Feature::D3D9_COMPOSITING);
  aOut->prefs().useD2D1() = gfxConfig::GetValue(Feature::DIRECT2D);

  if (d3d11.IsEnabled()) {
    DeviceManagerDx* dm = DeviceManagerDx::Get();
    dm->ExportDeviceInfo(&aOut->d3d11());
  }
}

bool gfxWindowsPlatform::SupportsPluginDirectDXGIDrawing() {
  // Ensure devices initialization for plugin's DXGISurface. The devices are
  // lazily initialized with WebRender to reduce memory usage.
  EnsureDevicesInitialized();

  DeviceManagerDx* dm = DeviceManagerDx::Get();
  if (!dm->GetContentDevice() || !dm->TextureSharingWorks()) {
    return false;
  }
  return true;
}

bool gfxWindowsPlatform::CheckVariationFontSupport() {
  // Variation font support is only available on Fall Creators Update or later.
  return IsWin10FallCreatorsUpdateOrLater();
}
