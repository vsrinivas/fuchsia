// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/skia/skia_vmo_image.h"

#include "lib/ftl/logging.h"

static_assert(sizeof(mx_size_t) == sizeof(uint64_t),
              "Fuchsia should always be 64-bit");

namespace mozart {
namespace {

void ReleaseBuffer(const void* pixels, void* context) {
  delete static_cast<ConsumedBufferHolder*>(context);
}

sk_sp<SkImage> MakeSkImageInternal(
    const SkImageInfo& info,
    size_t row_bytes,
    std::unique_ptr<ConsumedBufferHolder> buffer_holder,
    std::unique_ptr<BufferFence>* out_fence) {
  FTL_DCHECK(buffer_holder);

  size_t needed_bytes = info.height() * row_bytes;
  if (!info.validRowBytes(row_bytes) ||
      buffer_holder->shared_vmo()->vmo_size() < needed_bytes) {
    FTL_LOG(ERROR) << "Invalid image metadata";
    return nullptr;
  }

  void* buffer = buffer_holder->shared_vmo()->Map();
  if (!buffer) {
    FTL_LOG(ERROR) << "Could not map image into memory";
    return nullptr;
  }

  SkPixmap sk_pixmap(info, buffer, row_bytes);
  sk_sp<SkImage> image =
      SkImage::MakeFromRaster(sk_pixmap, &ReleaseBuffer, buffer_holder.get());
  if (!image) {
    FTL_LOG(ERROR) << "Could not create SkImage";
    return nullptr;
  }

  *out_fence = buffer_holder->TakeFence();
  buffer_holder.release();  // now owned by SkImage
  return image;
}

}  // namespace

sk_sp<SkImage> MakeSkImage(ImagePtr image,
                           BufferConsumer* consumer,
                           std::unique_ptr<BufferFence>* out_fence) {
  FTL_DCHECK(image);
  FTL_DCHECK(image->size);
  FTL_DCHECK(image->buffer);
  FTL_DCHECK(consumer);
  FTL_DCHECK(consumer->map_flags() & MX_VM_FLAG_PERM_READ);

  SkColorType sk_color_type;
  switch (image->pixel_format) {
    case Image::PixelFormat::B8G8R8A8:
      sk_color_type = kBGRA_8888_SkColorType;
      break;
    default:
      FTL_LOG(ERROR) << "Unsupported pixel format: " << image->pixel_format;
      return nullptr;
  }

  SkAlphaType sk_alpha_type;
  switch (image->alpha_format) {
    case Image::AlphaFormat::OPAQUE:
      sk_alpha_type = kOpaque_SkAlphaType;
      break;
    case Image::AlphaFormat::PREMULTIPLIED:
      sk_alpha_type = kPremul_SkAlphaType;
      break;
    case Image::AlphaFormat::NON_PREMULTIPLIED:
      sk_alpha_type = kUnpremul_SkAlphaType;
      break;
    default:
      FTL_LOG(ERROR) << "Unsupported alpha format: " << image->alpha_format;
      return nullptr;
  }

  sk_sp<SkColorSpace> sk_color_space;
  switch (image->color_space) {
    case Image::ColorSpace::SRGB:
      sk_color_space = SkColorSpace::NewNamed(SkColorSpace::kSRGB_Named);
      break;
    default:
      FTL_LOG(ERROR) << "Unsupported color space: " << image->color_space;
      return nullptr;
  }

  return MakeSkImageFromBuffer(
      SkImageInfo::Make(image->size->width, image->size->height, sk_color_type,
                        sk_alpha_type, sk_color_space),
      image->stride, std::move(image->buffer), consumer, out_fence);
}

sk_sp<SkImage> MakeSkImageFromBuffer(const SkImageInfo& info,
                                     size_t row_bytes,
                                     BufferPtr buffer,
                                     BufferConsumer* consumer,
                                     std::unique_ptr<BufferFence>* out_fence) {
  FTL_DCHECK(buffer);
  FTL_DCHECK(consumer);
  FTL_DCHECK(consumer->map_flags() & MX_VM_FLAG_PERM_READ);

  return MakeSkImageInternal(
      info, row_bytes, consumer->ConsumeBuffer(std::move(buffer)), out_fence);
}

}  // namespace mozart
