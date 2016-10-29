// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/skia/skia_vmo_surface.h"

#include <mx/process.h>

#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"

static_assert(sizeof(mx_size_t) == sizeof(uint64_t),
              "Fuchsia should always be 64-bit");

namespace mozart {
namespace {

void UnmapMemory(void* pixels, void* context) {
  mx_status_t status =
      mx::process::self().unmap_vm(reinterpret_cast<uintptr_t>(pixels), 0u);
  FTL_CHECK(status == NO_ERROR);
}

sk_sp<SkSurface> MakeSkSurfaceFromVMOWithSize(const mx::vmo& vmo,
                                              const SkImageInfo& info,
                                              size_t row_bytes,
                                              size_t total_bytes) {
  size_t needed_bytes = info.height() * row_bytes;
  if (!info.validRowBytes(row_bytes) || total_bytes < needed_bytes) {
    FTL_LOG(ERROR) << "invalid image metadata";
    return nullptr;
  }

  uintptr_t buffer = 0u;
  mx_status_t status =
      mx::process::self().map_vm(vmo, 0u, needed_bytes, &buffer,
                                 MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE);
  if (status != NO_ERROR) {
    FTL_LOG(ERROR) << "mx::process::map_vm failed: status=" << status;
    return nullptr;
  }

  sk_sp<SkSurface> surface = SkSurface::MakeRasterDirectReleaseProc(
      info, reinterpret_cast<void*>(buffer), row_bytes, &UnmapMemory, nullptr);
  if (!surface) {
    FTL_LOG(ERROR) << "Could not create SkSurface";
    return nullptr;
  }
  return surface;
}

}  // namespace

sk_sp<SkSurface> MakeSkSurface(const SkImageInfo& info, ImagePtr* out_image) {
  FTL_DCHECK(out_image);

  size_t row_bytes = info.minRowBytes();
  size_t total_bytes = info.height() * row_bytes;

  Image::PixelFormat pixel_format;
  switch (info.colorType()) {
    case kBGRA_8888_SkColorType:
      pixel_format = Image::PixelFormat::B8G8R8A8;
      break;
    default:
      FTL_LOG(ERROR) << "Unsupported color type: " << info.colorType();
      return nullptr;
  }

  Image::AlphaFormat alpha_format;
  switch (info.alphaType()) {
    case kUnknown_SkAlphaType:
    case kOpaque_SkAlphaType:
      alpha_format = Image::AlphaFormat::OPAQUE;
      break;
    case kPremul_SkAlphaType:
      alpha_format = Image::AlphaFormat::PREMULTIPLIED;
      break;
    case kUnpremul_SkAlphaType:
      alpha_format = Image::AlphaFormat::NON_PREMULTIPLIED;
      break;
    default:
      FTL_LOG(ERROR) << "Unsupported alpha type: " << info.alphaType();
      return nullptr;
  }

  Image::ColorSpace color_space;
  if (!info.colorSpace() || info.gammaCloseToSRGB()) {
    // TODO(jeffbrown): Should we consider no color space as linear RGB?
    color_space = Image::ColorSpace::SRGB;
  } else {
    FTL_LOG(ERROR) << "Unsupported color space";
    return nullptr;
  }

  mx::vmo vmo;
  if (mx::vmo::create(total_bytes, 0, &vmo) < 0) {
    FTL_LOG(ERROR) << "mx::vmo::create failed";
    return nullptr;
  }

  // Optimization: We will be writing to every page of the buffer, so
  // allocate physical memory for it eagerly.
  mx_status_t status =
      vmo.op_range(MX_VMO_OP_COMMIT, 0u, total_bytes, nullptr, 0u);

  if (status != NO_ERROR) {
    FTL_LOG(ERROR) << "mx::vmo::op_range failed: status=" << status;
    return nullptr;
  }

  sk_sp<SkSurface> surface =
      MakeSkSurfaceFromVMOWithSize(vmo, info, row_bytes, total_bytes);
  if (!surface)
    return nullptr;

  auto image = Image::New();
  image->size = mojo::Size::New();
  image->size->width = info.width();
  image->size->height = info.height();
  image->stride = row_bytes;
  image->pixel_format = pixel_format;
  image->alpha_format = alpha_format;
  image->color_space = color_space;
  image->buffer.reset(mojo::SharedBufferHandle(vmo.release()));

  *out_image = std::move(image);
  return surface;
}

sk_sp<SkSurface> MakeSkSurface(const SkISize& size, ImagePtr* out_image) {
  return MakeSkSurface(
      SkImageInfo::Make(size.width(), size.height(), kBGRA_8888_SkColorType,
                        kPremul_SkAlphaType),
      out_image);
}

sk_sp<SkSurface> MakeSkSurface(const mojo::Size& size, ImagePtr* out_image) {
  return MakeSkSurface(SkISize::Make(size.width, size.height), out_image);
}

sk_sp<SkSurface> MakeSkSurfaceFromVMO(const mx::vmo& vmo,
                                      const SkImageInfo& info,
                                      size_t row_bytes) {
  uint64_t total_bytes = 0u;
  mx_status_t status = vmo.get_size(&total_bytes);
  if (status != NO_ERROR) {
    FTL_LOG(ERROR) << "mx::vmo::get_size failed: status=" << status;
    return nullptr;
  }

  return MakeSkSurfaceFromVMOWithSize(vmo, info, row_bytes, total_bytes);
}

}  // namespace mozart
