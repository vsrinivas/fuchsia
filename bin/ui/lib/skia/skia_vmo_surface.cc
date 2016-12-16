// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/skia/skia_vmo_surface.h"

#include <mx/vmar.h>

#include <atomic>

#include "apps/tracing/lib/trace/event.h"
#include "lib/ftl/logging.h"

static_assert(sizeof(size_t) == sizeof(uint64_t),
              "Fuchsia should always be 64-bit");

namespace mozart {
namespace {

std::atomic<int32_t> g_count;

void TraceCount(int32_t delta) {
  int32_t count = g_count.fetch_add(delta, std::memory_order_relaxed) + delta;
  TRACE_COUNTER("gfx", "SkSurfaceVmo", 0u, "count", count);
}

void ReleaseBuffer(void* pixels, void* context) {
  delete static_cast<ProducedBufferHolder*>(context);
  TraceCount(-1);
}

void UnmapMemory(void* pixels, void* context) {
  const size_t size = reinterpret_cast<size_t>(context);
  mx_status_t status =
      mx::vmar::root_self().unmap(reinterpret_cast<uintptr_t>(pixels), size);
  FTL_CHECK(status == NO_ERROR);
  TraceCount(-1);
}

sk_sp<SkSurface> MakeSkSurfaceInternal(
    const SkImageInfo& info,
    size_t row_bytes,
    std::unique_ptr<ProducedBufferHolder> buffer_holder) {
  FTL_DCHECK(buffer_holder);

  void* buffer = buffer_holder->shared_vmo()->Map();
  if (!buffer) {
    FTL_LOG(ERROR) << "Could not map surface into memory";
    return nullptr;
  }

  sk_sp<SkSurface> surface = SkSurface::MakeRasterDirectReleaseProc(
      info, buffer, row_bytes, &ReleaseBuffer, buffer_holder.get());
  if (!surface) {
    FTL_LOG(ERROR) << "Could not create SkSurface";
    return nullptr;
  }

  TraceCount(1);
  buffer_holder.release();  // now owned by SkSurface
  return surface;
}

}  // namespace

sk_sp<SkSurface> MakeSkSurface(const SkISize& size,
                               BufferProducer* producer,
                               ImagePtr* out_image) {
  return MakeSkSurface(
      SkImageInfo::Make(size.width(), size.height(), kBGRA_8888_SkColorType,
                        kPremul_SkAlphaType),
      producer, out_image);
}

sk_sp<SkSurface> MakeSkSurface(const Size& size,
                               BufferProducer* producer,
                               ImagePtr* out_image) {
  return MakeSkSurface(SkISize::Make(size.width, size.height), producer,
                       out_image);
}

sk_sp<SkSurface> MakeSkSurface(const SkImageInfo& info,
                               BufferProducer* producer,
                               ImagePtr* out_image) {
  FTL_DCHECK(producer);
  FTL_DCHECK(producer->map_flags() &
             (MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE));
  FTL_DCHECK(out_image);

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

  size_t row_bytes = info.minRowBytes();
  size_t total_bytes = row_bytes * info.height();
  auto buffer_holder = producer->ProduceBuffer(total_bytes);
  if (!buffer_holder) {
    FTL_LOG(ERROR) << "Could not produce buffer: total_bytes=" << total_bytes;
    return nullptr;
  }

  BufferPtr buffer = buffer_holder->GetBuffer();
  if (!buffer) {
    FTL_LOG(ERROR) << "Could not get buffer for consumer";
    return nullptr;
  }

  sk_sp<SkSurface> surface =
      MakeSkSurfaceInternal(info, row_bytes, std::move(buffer_holder));
  if (!surface)
    return nullptr;

  auto image = Image::New();
  image->size = Size::New();
  image->size->width = info.width();
  image->size->height = info.height();
  image->stride = row_bytes;
  image->pixel_format = pixel_format;
  image->alpha_format = alpha_format;
  image->color_space = color_space;
  image->buffer = std::move(buffer);
  *out_image = std::move(image);
  return surface;
}

sk_sp<SkSurface> MakeSkSurfaceFromVMO(const SkImageInfo& info,
                                      size_t row_bytes,
                                      const mx::vmo& vmo) {
  FTL_DCHECK(vmo);

  uint64_t total_bytes = 0u;
  mx_status_t status = vmo.get_size(&total_bytes);
  FTL_CHECK(status == NO_ERROR);

  size_t needed_bytes = info.height() * row_bytes;
  if (!info.validRowBytes(row_bytes) || total_bytes < needed_bytes) {
    FTL_LOG(ERROR) << "Invalid image metadata: total_bytes=" << total_bytes
                   << ", needed_bytes=" << needed_bytes;
    return nullptr;
  }

  uintptr_t buffer = 0u;
  status =
      mx::vmar::root_self().map(0, vmo, 0u, needed_bytes,
                                MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                                &buffer);
  if (status != NO_ERROR) {
    FTL_LOG(ERROR) << "Could not map surface: status=" << status;
    return nullptr;
  }

  sk_sp<SkSurface> surface = SkSurface::MakeRasterDirectReleaseProc(
      info, reinterpret_cast<void*>(buffer), row_bytes, &UnmapMemory,
      reinterpret_cast<void*>(needed_bytes));
  if (!surface) {
    FTL_LOG(ERROR) << "Could not create SkSurface";
    status = mx::vmar::root_self().unmap(buffer, needed_bytes);
    FTL_CHECK(status == NO_ERROR);
    return nullptr;
  }

  TraceCount(1);
  return surface;
}

}  // namespace mozart
