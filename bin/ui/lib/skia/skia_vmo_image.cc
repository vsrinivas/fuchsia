// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/skia/skia_vmo_image.h"

#include <atomic>

#ifdef MOZART_USE_VULKAN
#include "apps/mozart/lib/skia/vk_vmo_image_generator.h"
#endif
#include "apps/tracing/lib/trace/event.h"
#include "lib/ftl/logging.h"

static_assert(sizeof(size_t) == sizeof(uint64_t),
              "Fuchsia should always be 64-bit");

namespace mozart {
namespace {

std::atomic<int32_t> g_count;

void TraceCount(int32_t delta) {
  int32_t count = g_count.fetch_add(delta, std::memory_order_relaxed) + delta;
  TRACE_COUNTER("gfx", "SkImageVmo", 0u, "count", count);
}

void ReleaseBuffer(const void* pixels, void* context) {
  delete static_cast<ConsumedBufferHolder*>(context);
  TraceCount(-1);
}

sk_sp<SkImage> MakeSkImageInternal(
    const SkImageInfo& info,
    size_t row_bytes,
    std::unique_ptr<ConsumedBufferHolder> buffer_holder,
    std::unique_ptr<BufferFence>* out_fence) {
  FTL_DCHECK(buffer_holder);
  FTL_DCHECK(out_fence);

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

  TraceCount(1);
  *out_fence = buffer_holder->TakeFence();
  buffer_holder.release();  // now owned by SkImage
  return image;
}

#ifdef MOZART_USE_VULKAN
sk_sp<SkImage> MakeSkImageFromVkDeviceMemoryInternal(
    const SkImageInfo& info,
    size_t row_bytes,
    std::unique_ptr<ConsumedBufferHolder> buffer_holder,
    std::unique_ptr<BufferFence>* out_fence) {
  FTL_DCHECK(buffer_holder);
  FTL_DCHECK(out_fence);

  // Create a generator because we need to use the rasterizer's GrContext
  // to create an Image (GrContext doesn't support multithreaded use).
  auto vk_vmo_image_generator =
      std::make_unique<VkVmoImageGenerator>(info, buffer_holder->shared_vmo());
  sk_sp<SkImage> image = sk_sp<SkImage>(
      SkImage::MakeFromGenerator(std::move(vk_vmo_image_generator)));
  if (!image) {
    FTL_LOG(ERROR) << "Could not create SkImage.";
    return nullptr;
  }

  *out_fence = buffer_holder->TakeFence();
  return image;
};
#endif  // MOZART_USE_VULKAN

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
      sk_color_space = SkColorSpace::MakeSRGB();
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

  Buffer::MemoryType buffer_memory_type = buffer->memory_type;
  auto buffer_holder = consumer->ConsumeBuffer(std::move(buffer));
  if (!buffer_holder)
    return nullptr;

  switch (buffer_memory_type) {
    case Buffer::MemoryType::VK_DEVICE_MEMORY:
#ifdef MOZART_USE_VULKAN
      return MakeSkImageFromVkDeviceMemoryInternal(
          info, row_bytes, std::move(buffer_holder), out_fence);
// Fall through if we aren't using Vulkan within Mozart.
#endif
    case Buffer::MemoryType::DEVICE_MEMORY:
      return MakeSkImageInternal(info, row_bytes, std::move(buffer_holder),
                                 out_fence);
    default:
      FTL_DCHECK(false);
      return nullptr;
  }
}

}  // namespace mozart
