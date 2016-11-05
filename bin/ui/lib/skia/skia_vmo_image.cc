// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/skia/skia_vmo_image.h"

#include <mx/process.h>

#include "lib/ftl/logging.h"

static_assert(sizeof(mx_size_t) == sizeof(uint64_t),
              "Fuchsia should always be 64-bit");

namespace mozart {
namespace {

void UnmapMemory(const void* pixels, void* context) {
  mx_status_t status =
      mx::process::self().unmap_vm(reinterpret_cast<uintptr_t>(pixels), 0u);
  FTL_CHECK(status == NO_ERROR);
}

sk_sp<SkImage> MakeSkImageFromVMOWithSize(const mx::vmo& vmo,
                                          const SkImageInfo& info,
                                          size_t row_bytes,
                                          size_t total_bytes) {
  size_t needed_bytes = info.height() * row_bytes;
  if (!info.validRowBytes(row_bytes) || total_bytes < needed_bytes) {
    FTL_LOG(ERROR) << "invalid image metadata";
    return nullptr;
  }

  uintptr_t buffer = 0u;
  mx_status_t status = mx::process::self().map_vm(
      vmo, 0u, needed_bytes, &buffer, MX_VM_FLAG_PERM_READ);
  if (status != NO_ERROR) {
    FTL_LOG(ERROR) << "mx::process::map_vm failed: status=" << status;
    return nullptr;
  }

  SkPixmap sk_pixmap(info, reinterpret_cast<void*>(buffer), row_bytes);
  sk_sp<SkImage> image =
      SkImage::MakeFromRaster(sk_pixmap, &UnmapMemory, nullptr);
  if (!image) {
    FTL_LOG(ERROR) << "Could not create SkImage";
    return nullptr;
  }
  return image;
}

}  // namespace

sk_sp<SkImage> MakeSkImage(ImagePtr image) {
  FTL_DCHECK(image);
  FTL_DCHECK(image->size);

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

  return MakeSkImageFromVMO(
      std::move(image->buffer),
      SkImageInfo::Make(image->size->width, image->size->height, sk_color_type,
                        sk_alpha_type, sk_color_space),
      image->stride);
}

sk_sp<SkImage> MakeSkImageFromVMO(const mx::vmo& vmo,
                                  const SkImageInfo& info,
                                  size_t row_bytes) {
  uint64_t total_bytes = 0u;
  mx_status_t status = vmo.get_size(&total_bytes);
  if (status != NO_ERROR) {
    FTL_LOG(ERROR) << "mx::vmo::get_size failed: status=" << status;
    return nullptr;
  }

  return MakeSkImageFromVMOWithSize(vmo, info, row_bytes, total_bytes);
}

}  // namespace mozart
