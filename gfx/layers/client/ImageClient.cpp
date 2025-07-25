/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageClient.h"

#include <stdint.h>  // for uint32_t

#include "ClientLayerManager.h"  // for ClientLayer
#include "ImageContainer.h"      // for Image, PlanarYCbCrImage, etc
#include "ImageTypes.h"          // for ImageFormat::PLANAR_YCBCR, etc
#include "GLImages.h"            // for SurfaceTextureImage::Data, etc
#include "gfx2DGlue.h"           // for ImageFormatToSurfaceFormat
#include "gfxPlatform.h"         // for gfxPlatform
#include "mozilla/Assertions.h"  // for MOZ_ASSERT, etc
#include "mozilla/RefPtr.h"      // for RefPtr, already_AddRefed
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/BaseSize.h"               // for BaseSize
#include "mozilla/gfx/Point.h"                  // for IntSize
#include "mozilla/gfx/Types.h"                  // for SurfaceFormat, etc
#include "mozilla/layers/CompositableClient.h"  // for CompositableClient
#include "mozilla/layers/CompositableForwarder.h"
#include "mozilla/layers/CompositorTypes.h"  // for CompositableType, etc
#include "mozilla/layers/ISurfaceAllocator.h"
#include "mozilla/layers/LayersSurfaces.h"    // for SurfaceDescriptor, etc
#include "mozilla/layers/ShadowLayers.h"      // for ShadowLayerForwarder
#include "mozilla/layers/TextureClient.h"     // for TextureClient, etc
#include "mozilla/layers/TextureClientOGL.h"  // for SurfaceTextureClient
#include "mozilla/mozalloc.h"                 // for operator delete, etc
#include "nsCOMPtr.h"                         // for already_AddRefed
#include "nsDebug.h"                          // for NS_WARNING, NS_ASSERTION
#include "nsISupportsImpl.h"                  // for Image::Release, etc
#include "nsRect.h"                           // for mozilla::gfx::IntRect

namespace mozilla {
namespace layers {

using namespace mozilla::gfx;

/* static */
already_AddRefed<ImageClient> ImageClient::CreateImageClient(
    CompositableType aCompositableHostType, CompositableForwarder* aForwarder,
    TextureFlags aFlags) {
  RefPtr<ImageClient> result = nullptr;
  switch (aCompositableHostType) {
    case CompositableType::IMAGE:
      result =
          new ImageClientSingle(aForwarder, aFlags, CompositableType::IMAGE);
      break;
    case CompositableType::IMAGE_BRIDGE:
      result = new ImageClientBridge(aForwarder, aFlags);
      break;
    case CompositableType::UNKNOWN:
      result = nullptr;
      break;
    default:
      MOZ_CRASH("GFX: unhandled program type image");
  }

  NS_ASSERTION(result, "Failed to create ImageClient");

  return result.forget();
}

void ImageClient::RemoveTexture(TextureClient* aTexture
#ifdef MOZ_BUILD_WEBRENDER
                                ,
                                const Maybe<wr::RenderRoot>& aRenderRoot
#endif
) {
  GetForwarder()->RemoveTextureFromCompositable(this, aTexture
#ifdef MOZ_BUILD_WEBRENDER
                                                ,
                                                aRenderRoot
#endif
  );
}

ImageClientSingle::ImageClientSingle(CompositableForwarder* aFwd,
                                     TextureFlags aFlags,
                                     CompositableType aType)
    : ImageClient(aFwd, aFlags, aType) {}

TextureInfo ImageClientSingle::GetTextureInfo() const {
  return TextureInfo(CompositableType::IMAGE);
}

void ImageClientSingle::FlushAllImages() {
  for (auto& b : mBuffers) {
    // It should be safe to just assume a default render root here, even if
    // the texture actually presents in a content render root, as the only
    // risk would be if the content render root has not / is not going to
    // generate a frame before the texture gets cleared.
    RemoveTexture(b.mTextureClient
#ifdef MOZ_BUILD_WEBRENDER
                  ,
                  Some(wr::RenderRoot::Default)
#endif
    );
  }
  mBuffers.Clear();
}

/* static */
already_AddRefed<TextureClient> ImageClient::CreateTextureClientForImage(
    Image* aImage, KnowsCompositor* aKnowsCompositor) {
  RefPtr<TextureClient> texture;
  if (aImage->GetFormat() == ImageFormat::PLANAR_YCBCR) {
    PlanarYCbCrImage* ycbcr = static_cast<PlanarYCbCrImage*>(aImage);
    const PlanarYCbCrData* data = ycbcr->GetData();
    if (!data) {
      return nullptr;
    }
    texture = TextureClient::CreateForYCbCr(
        aKnowsCompositor, data->mYSize, data->mYStride, data->mCbCrSize,
        data->mCbCrStride, data->mStereoMode, data->mColorDepth,
        data->mYUVColorSpace, data->mColorRange, TextureFlags::DEFAULT);
    if (!texture) {
      return nullptr;
    }

    TextureClientAutoLock autoLock(texture, OpenMode::OPEN_WRITE_ONLY);
    if (!autoLock.Succeeded()) {
      return nullptr;
    }

    bool status = UpdateYCbCrTextureClient(texture, *data);
    MOZ_ASSERT(status);
    if (!status) {
      return nullptr;
    }
#ifdef MOZ_WIDGET_ANDROID
  } else if (aImage->GetFormat() == ImageFormat::SURFACE_TEXTURE) {
    gfx::IntSize size = aImage->GetSize();
    SurfaceTextureImage* typedImage = aImage->AsSurfaceTextureImage();
    texture = AndroidSurfaceTextureData::CreateTextureClient(
        typedImage->GetHandle(), size, typedImage->GetContinuous(),
        typedImage->GetOriginPos(), typedImage->GetHasAlpha(),
        aKnowsCompositor->GetTextureForwarder(), TextureFlags::DEFAULT);
#endif
  } else {
    RefPtr<gfx::SourceSurface> surface = aImage->GetAsSourceSurface();
    MOZ_ASSERT(surface);
    texture = TextureClient::CreateForDrawing(
        aKnowsCompositor, surface->GetFormat(), aImage->GetSize(),
        BackendSelector::Content, TextureFlags::DEFAULT);
    if (!texture) {
      return nullptr;
    }

    MOZ_ASSERT(texture->CanExposeDrawTarget());

    if (!texture->Lock(OpenMode::OPEN_WRITE_ONLY)) {
      return nullptr;
    }

    {
      // We must not keep a reference to the DrawTarget after it has been
      // unlocked.
      DrawTarget* dt = texture->BorrowDrawTarget();
      if (!dt) {
        gfxWarning()
            << "ImageClientSingle::UpdateImage failed in BorrowDrawTarget";
        return nullptr;
      }
      MOZ_ASSERT(surface.get());
      dt->CopySurface(surface, IntRect(IntPoint(), surface->GetSize()),
                      IntPoint());
    }

    texture->Unlock();
  }
  return texture.forget();
}

bool ImageClientSingle::UpdateImage(ImageContainer* aContainer,
                                    uint32_t aContentFlags
#ifdef MOZ_BUILD_WEBRENDER
                                    ,
                                    const Maybe<wr::RenderRoot>& aRenderRoot
#endif
) {
  AutoTArray<ImageContainer::OwningImage, 4> images;
  uint32_t generationCounter;
  aContainer->GetCurrentImages(&images, &generationCounter);

  if (mLastUpdateGenerationCounter == generationCounter) {
    return true;
  }
  mLastUpdateGenerationCounter = generationCounter;

  // Don't try to update to invalid images.
  images.RemoveElementsBy(
      [](const auto& image) { return !image.mImage->IsValid(); });
  if (images.IsEmpty()) {
    // This can happen if a ClearAllImages raced with SetCurrentImages from
    // another thread and ClearImagesFromImageBridge ran after the
    // SetCurrentImages call but before UpdateImageClientNow.
    // This can also happen if all images in the list are invalid.
    // We return true because the caller would attempt to recreate the
    // ImageClient otherwise, and that isn't going to help.
    for (auto& b : mBuffers) {
      RemoveTexture(b.mTextureClient
#ifdef MOZ_BUILD_WEBRENDER
                    ,
                    aRenderRoot
#endif
      );
    }
    mBuffers.Clear();
    return true;
  }

  nsTArray<Buffer> newBuffers;
  AutoTArray<CompositableForwarder::TimedTextureClient, 4> textures;

  for (auto& img : images) {
    Image* image = img.mImage;

    RefPtr<TextureClient> texture = image->GetTextureClient(GetForwarder());
    const bool hasTextureClient = !!texture;

    for (int32_t i = mBuffers.Length() - 1; i >= 0; --i) {
      if (mBuffers[i].mImageSerial == image->GetSerial()) {
        if (hasTextureClient) {
          MOZ_ASSERT(image->GetTextureClient(GetForwarder()) ==
                     mBuffers[i].mTextureClient);
        } else {
          texture = mBuffers[i].mTextureClient;
        }
        // Remove this element from mBuffers so mBuffers only contains
        // images that aren't present in 'images'
        mBuffers.RemoveElementAt(i);
      }
    }

    if (!texture) {
      // Slow path, we should not be hitting it very often and if we do it means
      // we are using an Image class that is not backed by textureClient and we
      // should fix it.
      texture = CreateTextureClientForImage(image, GetForwarder());
    }

    if (!texture) {
      return false;
    }

    // We check if the texture's allocator is still open, since in between media
    // decoding a frame and adding it to the compositable, we could have
    // restarted the GPU process.
    if (!texture->GetAllocator()->IPCOpen()) {
      continue;
    }
    if (!AddTextureClient(texture)) {
      return false;
    }

    CompositableForwarder::TimedTextureClient* t = textures.AppendElement();
    t->mTextureClient = texture;
    t->mTimeStamp = img.mTimeStamp;
    t->mPictureRect = image->GetPictureRect();
    t->mFrameID = img.mFrameID;
    t->mProducerID = img.mProducerID;

    Buffer* newBuf = newBuffers.AppendElement();
    newBuf->mImageSerial = image->GetSerial();
    newBuf->mTextureClient = texture;

    texture->SyncWithObject(GetForwarder()->GetSyncObject());
  }

  GetForwarder()->UseTextures(this, textures
#ifdef MOZ_BUILD_WEBRENDER
                              ,
                              aRenderRoot
#endif
  );

  for (auto& b : mBuffers) {
    RemoveTexture(b.mTextureClient
#ifdef MOZ_BUILD_WEBRENDER
                  ,
                  aRenderRoot
#endif
    );
  }
  mBuffers.SwapElements(newBuffers);

  return true;
}

RefPtr<TextureClient> ImageClientSingle::GetForwardedTexture() {
  if (mBuffers.Length() == 0) {
    return nullptr;
  }
  return mBuffers[0].mTextureClient;
}

bool ImageClientSingle::AddTextureClient(TextureClient* aTexture) {
  MOZ_ASSERT((mTextureFlags & aTexture->GetFlags()) == mTextureFlags);
  return CompositableClient::AddTextureClient(aTexture);
}

void ImageClientSingle::OnDetach() { mBuffers.Clear(); }

ImageClient::ImageClient(CompositableForwarder* aFwd, TextureFlags aFlags,
                         CompositableType aType)
    : CompositableClient(aFwd, aFlags),
      mLayer(nullptr),
      mType(aType),
      mLastUpdateGenerationCounter(0) {}

ImageClientBridge::ImageClientBridge(CompositableForwarder* aFwd,
                                     TextureFlags aFlags)
    : ImageClient(aFwd, aFlags, CompositableType::IMAGE_BRIDGE) {}

bool ImageClientBridge::UpdateImage(ImageContainer* aContainer,
                                    uint32_t aContentFlags
#ifdef MOZ_BUILD_WEBRENDER
                                    ,
                                    const Maybe<wr::RenderRoot>& aRenderRoot
#endif
) {
  if (!GetForwarder() || !mLayer) {
    return false;
  }
  if (mAsyncContainerHandle == aContainer->GetAsyncContainerHandle()) {
    return true;
  }

  mAsyncContainerHandle = aContainer->GetAsyncContainerHandle();
  if (!mAsyncContainerHandle) {
    // If we couldn't contact a working ImageBridgeParent, just return.
    return true;
  }

  static_cast<ShadowLayerForwarder*>(GetForwarder())
      ->AttachAsyncCompositable(mAsyncContainerHandle, mLayer);
  return true;
}

}  // namespace layers
}  // namespace mozilla
