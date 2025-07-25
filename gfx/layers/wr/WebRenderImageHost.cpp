/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebRenderImageHost.h"

#include <utility>

#include "LayersLogging.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/layers/AsyncImagePipelineManager.h"
#include "mozilla/layers/Compositor.h"                // for Compositor
#include "mozilla/layers/CompositorVsyncScheduler.h"  // for CompositorVsyncScheduler
#include "mozilla/layers/Effects.h"  // for TexturedEffect, Effect, etc
#include "mozilla/layers/LayerManagerComposite.h"  // for TexturedEffect, Effect, etc
#include "mozilla/layers/WebRenderBridgeParent.h"
#include "mozilla/layers/WebRenderTextureHost.h"
#include "nsAString.h"
#include "nsDebug.h"          // for NS_WARNING, NS_ASSERTION
#include "nsPrintfCString.h"  // for nsPrintfCString
#include "nsString.h"         // for nsAutoCString

namespace mozilla {

using namespace gfx;

namespace layers {

class ISurfaceAllocator;

WebRenderImageHost::WebRenderImageHost(const TextureInfo& aTextureInfo)
    : CompositableHost(aTextureInfo),
      ImageComposite(),
      mCurrentAsyncImageManager(nullptr) {}

WebRenderImageHost::~WebRenderImageHost() { MOZ_ASSERT(mWrBridges.empty()); }

void WebRenderImageHost::UseTextureHost(
    const nsTArray<TimedTexture>& aTextures) {
  CompositableHost::UseTextureHost(aTextures);
  MOZ_ASSERT(aTextures.Length() >= 1);

  nsTArray<TimedImage> newImages;

  for (uint32_t i = 0; i < aTextures.Length(); ++i) {
    const TimedTexture& t = aTextures[i];
    MOZ_ASSERT(t.mTexture);
    if (i + 1 < aTextures.Length() && t.mProducerID == mLastProducerID &&
        t.mFrameID < mLastFrameID) {
      // Ignore frames before a frame that we already composited. We don't
      // ever want to display these frames. This could be important if
      // the frame producer adjusts timestamps (e.g. to track the audio clock)
      // and the new frame times are earlier.
      continue;
    }
    TimedImage& img = *newImages.AppendElement();
    img.mTextureHost = t.mTexture;
    img.mTimeStamp = t.mTimeStamp;
    img.mPictureRect = t.mPictureRect;
    img.mFrameID = t.mFrameID;
    img.mProducerID = t.mProducerID;
    img.mTextureHost->SetCropRect(img.mPictureRect);
    img.mTextureHost->Updated();
  }

  SetImages(std::move(newImages));

  if (GetAsyncRef()) {
    for (const auto& it : mWrBridges) {
      WebRenderBridgeParent* wrBridge = it.second;
      if (wrBridge && wrBridge->CompositorScheduler()) {
        wrBridge->CompositorScheduler()->ScheduleComposition();
      }
    }
  }

  // Video producers generally send replacement images with the same frameID but
  // slightly different timestamps in order to sync with the audio clock. This
  // means that any CompositeUntil() call we made in Composite() may no longer
  // guarantee that we'll composite until the next frame is ready. Fix that
  // here.
  if (mLastFrameID >= 0 && !mWrBridges.empty()) {
    for (const auto& img : Images()) {
      bool frameComesAfter =
          img.mFrameID > mLastFrameID || img.mProducerID != mLastProducerID;
      if (frameComesAfter && !img.mTimeStamp.IsNull()) {
        for (const auto& it : mWrBridges) {
          WebRenderBridgeParent* wrBridge = it.second;
          if (wrBridge) {
            wrBridge->AsyncImageManager()->CompositeUntil(
                img.mTimeStamp + TimeDuration::FromMilliseconds(BIAS_TIME_MS));
          }
        }
        break;
      }
    }
  }
}

void WebRenderImageHost::UseComponentAlphaTextures(
    TextureHost* aTextureOnBlack, TextureHost* aTextureOnWhite) {
  MOZ_ASSERT_UNREACHABLE("unexpected to be called");
}

void WebRenderImageHost::CleanupResources() {
  ClearImages();
  SetCurrentTextureHost(nullptr);
}

void WebRenderImageHost::RemoveTextureHost(TextureHost* aTexture) {
  CompositableHost::RemoveTextureHost(aTexture);
  RemoveImagesWithTextureHost(aTexture);
}

TimeStamp WebRenderImageHost::GetCompositionTime() const {
  TimeStamp time;

  MOZ_ASSERT(mCurrentAsyncImageManager);
  if (mCurrentAsyncImageManager) {
    time = mCurrentAsyncImageManager->GetCompositionTime();
  }
  return time;
}

TextureHost* WebRenderImageHost::GetAsTextureHost(IntRect* aPictureRect) {
  const TimedImage* img = ChooseImage();
  if (img) {
    return img->mTextureHost;
  }
  return nullptr;
}

TextureHost* WebRenderImageHost::GetAsTextureHostForComposite(
    AsyncImagePipelineManager* aAsyncImageManager) {
  mCurrentAsyncImageManager = aAsyncImageManager;
  const auto onExit =
      mozilla::MakeScopeExit([&]() { mCurrentAsyncImageManager = nullptr; });

  int imageIndex = ChooseImageIndex();
  if (imageIndex < 0) {
    SetCurrentTextureHost(nullptr);
    return nullptr;
  }

  if (uint32_t(imageIndex) + 1 < ImagesCount()) {
    mCurrentAsyncImageManager->CompositeUntil(
        GetImage(imageIndex + 1)->mTimeStamp +
        TimeDuration::FromMilliseconds(BIAS_TIME_MS));
  }

  const TimedImage* img = GetImage(imageIndex);

  if (mLastFrameID != img->mFrameID || mLastProducerID != img->mProducerID) {
    if (mAsyncRef) {
      ImageCompositeNotificationInfo info;
      info.mImageBridgeProcessId = mAsyncRef.mProcessId;
      info.mNotification = ImageCompositeNotification(
          mAsyncRef.mHandle, img->mTimeStamp,
          mCurrentAsyncImageManager->GetCompositionTime(), img->mFrameID,
          img->mProducerID);
      mCurrentAsyncImageManager->AppendImageCompositeNotification(info);
    }
    mLastFrameID = img->mFrameID;
    mLastProducerID = img->mProducerID;
  }
  SetCurrentTextureHost(img->mTextureHost);

  UpdateBias(imageIndex);

  return mCurrentTextureHost;
}

void WebRenderImageHost::SetCurrentTextureHost(TextureHost* aTexture) {
  if (aTexture == mCurrentTextureHost.get()) {
    return;
  }
  mCurrentTextureHost = aTexture;
}

void WebRenderImageHost::Attach(Layer* aLayer, TextureSourceProvider* aProvider,
                                AttachFlags aFlags) {}

void WebRenderImageHost::Composite(
    Compositor* aCompositor, LayerComposite* aLayer, EffectChain& aEffectChain,
    float aOpacity, const gfx::Matrix4x4& aTransform,
    const gfx::SamplingFilter aSamplingFilter, const gfx::IntRect& aClipRect,
    const nsIntRegion* aVisibleRegion, const Maybe<gfx::Polygon>& aGeometry) {
  MOZ_ASSERT_UNREACHABLE("unexpected to be called");
}

void WebRenderImageHost::SetTextureSourceProvider(
    TextureSourceProvider* aProvider) {
  if (mTextureSourceProvider != aProvider) {
    for (const auto& img : Images()) {
      img.mTextureHost->SetTextureSourceProvider(aProvider);
    }
  }
  CompositableHost::SetTextureSourceProvider(aProvider);
}

void WebRenderImageHost::PrintInfo(std::stringstream& aStream,
                                   const char* aPrefix) {
  aStream << aPrefix;
  aStream << nsPrintfCString("WebRenderImageHost (0x%p)", this).get();

  nsAutoCString pfx(aPrefix);
  pfx += "  ";
  for (const auto& img : Images()) {
    aStream << "\n";
    img.mTextureHost->PrintInfo(aStream, pfx.get());
    AppendToString(aStream, img.mPictureRect, " [picture-rect=", "]");
  }
}

void WebRenderImageHost::Dump(std::stringstream& aStream, const char* aPrefix,
                              bool aDumpHtml) {
  for (const auto& img : Images()) {
    aStream << aPrefix;
    aStream << (aDumpHtml ? "<ul><li>TextureHost: " : "TextureHost: ");
    DumpTextureHost(aStream, img.mTextureHost);
    aStream << (aDumpHtml ? " </li></ul> " : " ");
  }
}

already_AddRefed<gfx::DataSourceSurface> WebRenderImageHost::GetAsSurface() {
  const TimedImage* img = ChooseImage();
  if (img) {
    return img->mTextureHost->GetAsSurface();
  }
  return nullptr;
}

bool WebRenderImageHost::Lock() {
  MOZ_ASSERT_UNREACHABLE("unexpected to be called");
  return false;
}

void WebRenderImageHost::Unlock() {
  MOZ_ASSERT_UNREACHABLE("unexpected to be called");
}

IntSize WebRenderImageHost::GetImageSize() {
  const TimedImage* img = ChooseImage();
  if (img) {
    return IntSize(img->mPictureRect.Width(), img->mPictureRect.Height());
  }
  return IntSize();
}

void WebRenderImageHost::SetWrBridge(const wr::PipelineId& aPipelineId,
                                     WebRenderBridgeParent* aWrBridge) {
  MOZ_ASSERT(aWrBridge);
#ifdef DEBUG
  const auto it = mWrBridges.find(wr::AsUint64(aPipelineId));
  MOZ_ASSERT(it == mWrBridges.end());
#endif
  mWrBridges.emplace(wr::AsUint64(aPipelineId), aWrBridge);
}

void WebRenderImageHost::ClearWrBridge(const wr::PipelineId& aPipelineId,
                                       WebRenderBridgeParent* aWrBridge) {
  MOZ_ASSERT(aWrBridge);

  const auto it = mWrBridges.find(wr::AsUint64(aPipelineId));
  MOZ_ASSERT(it != mWrBridges.end());
  if (it == mWrBridges.end()) {
    gfxCriticalNote << "WrBridge mismatch happened";
    return;
  }
  mWrBridges.erase(it);
  SetCurrentTextureHost(nullptr);
}

}  // namespace layers
}  // namespace mozilla
