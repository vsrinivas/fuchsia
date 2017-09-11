// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/resources/image.h"

#include "garnet/bin/ui/scene_manager/engine/session.h"
#include "garnet/bin/ui/scene_manager/resources/gpu_memory.h"
#include "garnet/bin/ui/scene_manager/resources/host_memory.h"
#include "escher/util/image_utils.h"

namespace scene_manager {

const ResourceTypeInfo Image::kTypeInfo = {
    ResourceType::kImage | ResourceType::kImageBase, "Image"};

Image::Image(Session* session,
             scenic::ResourceId id,
             MemoryPtr memory,
             escher::ImagePtr image)
    : ImageBase(session, id, Image::kTypeInfo),
      memory_(std::move(memory)),
      image_(std::move(image)) {}

Image::Image(Session* session,
             scenic::ResourceId id,
             GpuMemoryPtr memory,
             escher::ImageInfo image_info,
             vk::Image vk_image)
    : ImageBase(session, id, Image::kTypeInfo),
      memory_(std::move(memory)),
      image_(fxl::MakeRefCounted<escher::Image>(
          session->engine()->escher_resource_recycler(),
          image_info,
          vk_image,
          static_cast<GpuMemory*>(memory_.get())->escher_gpu_mem())) {}

ImagePtr Image::New(Session* session,
                    scenic::ResourceId id,
                    MemoryPtr memory,
                    const scenic::ImageInfoPtr& image_info,
                    uint64_t memory_offset,
                    ErrorReporter* error_reporter) {
  vk::Format pixel_format = vk::Format::eUndefined;
  size_t bytes_per_pixel;
  size_t pixel_alignment;
  switch (image_info->pixel_format) {
    case scenic::ImageInfo::PixelFormat::BGRA_8:
      pixel_format = vk::Format::eB8G8R8A8Unorm;
      bytes_per_pixel = 4u;
      pixel_alignment = 4u;
      break;
  }

  if (image_info->width <= 0) {
    error_reporter->ERROR()
        << "Image::CreateFromMemory(): width must be greater than 0.";
    return nullptr;
  }
  if (image_info->height <= 0) {
    error_reporter->ERROR()
        << "Image::CreateFromMemory(): height must be greater than 0.";
    return nullptr;
  }

  auto& caps = session->engine()->escher()->device()->caps();
  if (image_info->width > caps.max_image_width) {
    error_reporter->ERROR()
        << "Image::CreateFromMemory(): image width exceeds maximum ("
        << image_info->width << " vs. " << caps.max_image_width << ").";
    return nullptr;
  }
  if (image_info->height > caps.max_image_height) {
    error_reporter->ERROR()
        << "Image::CreateFromMemory(): image height exceeds maximum ("
        << image_info->height << " vs. " << caps.max_image_height << ").";
    return nullptr;
  }

  // Create from host memory.
  if (memory->IsKindOf<HostMemory>()) {
    auto host_memory = memory->As<HostMemory>();

    if (image_info->stride < image_info->width * bytes_per_pixel) {
      error_reporter->ERROR()
          << "Image::CreateFromMemory(): stride too small for width.";
      return nullptr;
    }
    if (image_info->stride % pixel_alignment != 0) {
      error_reporter->ERROR()
          << "Image::CreateFromMemory(): stride must preserve pixel alignment.";
      return nullptr;
    }
    if (image_info->tiling != scenic::ImageInfo::Tiling::LINEAR) {
      error_reporter->ERROR()
          << "Image::CreateFromMemory(): tiling must be LINEAR for images "
          << "created using host memory.";
      return nullptr;
    }

    size_t image_size = image_info->height * image_info->stride;
    if (memory_offset >= host_memory->size()) {
      error_reporter->ERROR()
          << "Image::CreateFromMemory(): the offset of the Image must be "
          << "within the range of the Memory";
      return nullptr;
    }

    if (memory_offset + image_size > host_memory->size()) {
      error_reporter->ERROR()
          << "Image::CreateFromMemory(): the Image must fit within the size "
          << "of the Memory";
      return nullptr;
    }

    // TODO(MZ-141): Support non-minimal strides.
    if (image_info->stride != image_info->width * bytes_per_pixel) {
      error_reporter->ERROR()
          << "Image::CreateFromMemory(): the stride must be minimal (MZ-141)";
      return nullptr;
    }

    auto escher_image = escher::image_utils::NewImageFromPixels(
        session->engine()->escher_image_factory(),
        session->engine()->escher_gpu_uploader(), pixel_format,
        image_info->width, image_info->height,
        static_cast<uint8_t*>(host_memory->memory_base()) + memory_offset);
    return fxl::AdoptRef(new Image(session, id, std::move(host_memory),
                                   std::move(escher_image)));

    // Create from GPU memory.
  } else if (memory->IsKindOf<GpuMemory>()) {
    auto gpu_memory = memory->As<GpuMemory>();

    escher::ImageInfo escher_image_info;
    escher_image_info.format = pixel_format;
    escher_image_info.width = image_info->width;
    escher_image_info.height = image_info->height;
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

    if (memory_offset >= gpu_memory->size()) {
      error_reporter->ERROR()
          << "Image::CreateFromMemory(): the offset of the Image must be "
          << "within the range of the Memory";
      return nullptr;
    }

    if (memory_offset + memory_reqs.size > gpu_memory->size()) {
      error_reporter->ERROR()
          << "Image::CreateFromMemory(): the Image must fit within the size "
          << "of the Memory";
      return nullptr;
    }

    vk::DeviceMemory vk_mem = gpu_memory->escher_gpu_mem()->base();
    VkDeviceSize offset = memory_offset;
    vk_device.bindImageMemory(vk_image, vk_mem, offset);
    return fxl::AdoptRef(new Image(session, id, std::move(gpu_memory),
                                   escher_image_info, vk_image));
  } else {
    FXL_CHECK(false);
    return nullptr;
  }
}

ImagePtr Image::NewForTesting(Session* session,
                              scenic::ResourceId id,
                              escher::ResourceManager* image_owner,
                              MemoryPtr host_memory) {
  escher::ImagePtr escher_image = fxl::MakeRefCounted<escher::Image>(
      image_owner, escher::ImageInfo(), vk::Image(), nullptr);

  return fxl::AdoptRef(new Image(session, id, host_memory, escher_image));
}

}  // namespace scene_manager
