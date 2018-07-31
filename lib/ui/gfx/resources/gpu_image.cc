// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/gpu_image.h"

#include "garnet/lib/ui/gfx/engine/session.h"
#include "garnet/lib/ui/gfx/resources/gpu_memory.h"
#include "garnet/lib/ui/gfx/resources/host_memory.h"
#include "lib/escher/util/image_utils.h"

namespace scenic {
namespace gfx {

const ResourceTypeInfo GpuImage::kTypeInfo = {
    ResourceType::kGpuImage | ResourceType::kImage | ResourceType::kImageBase,
    "GpuImage"};

GpuImage::GpuImage(Session* session, scenic::ResourceId id, GpuMemoryPtr memory,
                   uint64_t memory_offset, escher::ImageInfo image_info,
                   vk::Image vk_image)
    : Image(session, id, GpuImage::kTypeInfo), memory_(std::move(memory)) {
  image_ = escher::Image::New(session->engine()->escher_resource_recycler(),
                              image_info, vk_image, memory_->escher_gpu_mem(),
                              memory_offset);
  FXL_CHECK(image_);
}

GpuImagePtr GpuImage::New(Session* session, scenic::ResourceId id,
                          GpuMemoryPtr memory,
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

  auto& caps = session->engine()->escher()->device()->caps();
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
  escher_image_info.usage =
      vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
  escher_image_info.memory_flags = vk::MemoryPropertyFlagBits::eDeviceLocal;

  vk::Device vk_device = session->engine()->vk_device();
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
        << "of the Memory";
    return nullptr;
  }

  return fxl::AdoptRef(new GpuImage(session, id, std::move(memory),
                                    memory_offset, escher_image_info,
                                    vk_image));
}

bool GpuImage::UpdatePixels() { return false; }

}  // namespace gfx
}  // namespace scenic
