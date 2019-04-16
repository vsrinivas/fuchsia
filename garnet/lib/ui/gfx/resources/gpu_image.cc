// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/gpu_image.h"

#include "garnet/lib/ui/gfx/engine/session.h"
#include "garnet/lib/ui/gfx/resources/memory.h"
#include "lib/escher/impl/naive_image.h"
#include "lib/escher/util/image_utils.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo GpuImage::kTypeInfo = {
    ResourceType::kGpuImage | ResourceType::kImage | ResourceType::kImageBase,
    "GpuImage"};

GpuImage::GpuImage(Session* session, ResourceId id, escher::GpuMemPtr gpu_mem,
                   escher::ImageInfo image_info, vk::Image vk_image)
    : Image(session, id, GpuImage::kTypeInfo) {
  image_ = escher::impl::NaiveImage::AdoptVkImage(
      session->resource_context().escher_resource_recycler, image_info,
      vk_image, std::move(gpu_mem));
  FXL_CHECK(image_);
}

GpuImagePtr GpuImage::New(Session* session, ResourceId id, MemoryPtr memory,
                          const fuchsia::images::ImageInfo& image_info,
                          uint64_t memory_offset,
                          ErrorReporter* error_reporter) {
  vk::Format pixel_format = vk::Format::eUndefined;
  size_t bytes_per_pixel;
  size_t pixel_alignment;
  switch (image_info.pixel_format) {
    case fuchsia::images::PixelFormat::BGRA_8:
      pixel_format = vk::Format::eB8G8R8A8Unorm;
      bytes_per_pixel = 4u;
      pixel_alignment = 4u;
      break;
    case fuchsia::images::PixelFormat::YUY2:
    case fuchsia::images::PixelFormat::NV12:
    case fuchsia::images::PixelFormat::YV12:
      error_reporter->ERROR()
          << "GpuImage::CreateFromMemory(): PixelFormat must be BGRA_8.";
      return nullptr;
  }

  if (image_info.width <= 0) {
    error_reporter->ERROR()
        << "GpuImage::CreateFromMemory(): width must be greater than 0.";
    return nullptr;
  }
  if (image_info.height <= 0) {
    error_reporter->ERROR()
        << "GpuImage::CreateFromMemory(): height must be greater than 0.";
    return nullptr;
  }

  auto& caps = session->resource_context().vk_device_queues_capabilities;
  if (image_info.width > caps.max_image_width) {
    error_reporter->ERROR()
        << "GpuImage::CreateFromMemory(): image width exceeds maximum ("
        << image_info.width << " vs. " << caps.max_image_width << ").";
    return nullptr;
  }
  if (image_info.height > caps.max_image_height) {
    error_reporter->ERROR()
        << "GpuImage::CreateFromMemory(): image height exceeds maximum ("
        << image_info.height << " vs. " << caps.max_image_height << ").";
    return nullptr;
  }

  escher::ImageInfo escher_image_info;
  escher_image_info.format = pixel_format;
  escher_image_info.width = image_info.width;
  escher_image_info.height = image_info.height;
  escher_image_info.sample_count = 1;
  // If this image is shared cross-process these flags (and all other
  // vkCreateImage parameters) need to match those in the other process.
  // Other locations that need to match: topaz/flutter_runner/vulkan_surface.cc
  escher_image_info.usage = vk::ImageUsageFlagBits::eTransferSrc |
                            vk::ImageUsageFlagBits::eTransferDst |
                            vk::ImageUsageFlagBits::eSampled |
                            vk::ImageUsageFlagBits::eColorAttachment;
  // TODO(SCN-1182): Add unit tests to verify this logic.
  switch (image_info.tiling) {
    case fuchsia::images::Tiling::LINEAR:
      escher_image_info.tiling = vk::ImageTiling::eLinear;
      break;
    case fuchsia::images::Tiling::GPU_OPTIMAL:
      escher_image_info.tiling = vk::ImageTiling::eOptimal;
      break;
  }
  // TODO(SCN-1012): Don't hardcode this -- use the data on the memory
  // object once we support a bitmask instead of an enum.
  escher_image_info.memory_flags = vk::MemoryPropertyFlagBits::eDeviceLocal;

  vk::Device vk_device = session->resource_context().vk_device;
  vk::Image vk_image =
      escher::image_utils::CreateVkImage(vk_device, escher_image_info);

  // Make sure that the image is within range of its associated memory.
  vk::MemoryRequirements memory_reqs;
  vk_device.getImageMemoryRequirements(vk_image, &memory_reqs);

  if (memory_offset >= memory->size()) {
    error_reporter->ERROR()
        << "GpuImage::CreateFromMemory(): the offset of the Image must be "
        << "within the range of the Memory";
    return nullptr;
  }

  if (memory_offset + memory_reqs.size > memory->size()) {
    error_reporter->ERROR()
        << "GpuImage::CreateFromMemory(): the Image must fit within the size "
        << "of the Memory" << memory_reqs.size << " " << memory->size() << " "
        << memory_offset;
    return nullptr;
  }

  // Make a pointer to a subregion of the memory, if necessary.
  escher::GpuMemPtr gpu_mem =
      (memory_offset > 0 || memory_reqs.size < memory->size())
          ? memory->GetGpuMem()->Suballocate(memory_reqs.size, memory_offset)
          : memory->GetGpuMem();

  return fxl::AdoptRef(new GpuImage(session, id, std::move(gpu_mem),
                                    escher_image_info, vk_image));
}

bool GpuImage::UpdatePixels(escher::BatchGpuUploader* uploader) {
  return false;
}

}  // namespace gfx
}  // namespace scenic_impl
