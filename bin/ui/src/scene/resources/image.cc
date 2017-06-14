// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene/resources/image.h"

#include "apps/mozart/src/scene/resources/gpu_memory.h"
#include "apps/mozart/src/scene/resources/host_memory.h"
#include "apps/mozart/src/scene/session/session.h"
#include "escher/util/image_utils.h"

namespace mozart {
namespace scene {

const ResourceTypeInfo Image::kTypeInfo = {ResourceType::kImage, "Image"};

Image::Image(Session* session, escher::ImagePtr image, HostMemoryPtr memory)
    : Resource(session, Image::kTypeInfo), memory_(memory), image_(image) {}

Image::Image(Session* session,
             escher::ImageInfo image_info,
             vk::Image vk_image,
             GpuMemoryPtr memory)
    : Resource(session, Image::kTypeInfo),
      memory_(memory),
      image_(ftl::MakeRefCounted<escher::Image>(
          session->context()->escher_resource_recycler(),
          image_info,
          vk_image,
          memory->escher_gpu_mem())) {}

ImagePtr Image::New(Session* session,
                    MemoryPtr memory,
                    const mozart2::ImagePtr& args,
                    ErrorReporter* error_reporter) {
  vk::Format pixel_format = vk::Format::eUndefined;
  size_t pixel_size;
  switch (args->info->pixel_format) {
    case mozart2::ImageInfo::PixelFormat::BGRA_8:
      pixel_format = vk::Format::eB8G8R8A8Unorm;
      pixel_size = sizeof(uint8_t);
      break;
  }
  if (args->info->width <= 0) {
    error_reporter->ERROR()
        << "Image::CreateFromMemory(): width must be greater than 0.";
    return nullptr;
  }
  if (args->info->height <= 0) {
    error_reporter->ERROR()
        << "Image::CreateFromMemory(): height must be greater than 0.";
    return nullptr;
  }
  // TODO: handle stride that does not match width
  if (args->info->width != args->info->stride) {
    error_reporter->ERROR()
        << "Image::CreateFromMemory(): stride must match width.";
    return nullptr;
  }

  // Create from host memory.
  if (memory->IsKindOf<HostMemory>()) {
    auto host_memory = memory->As<HostMemory>();

    if (args->info->tiling != mozart2::ImageInfo::Tiling::LINEAR) {
      error_reporter->ERROR()
          << "Image::CreateFromMemory(): tiling must be LINEAR for images "
          << "created using host memory.";
      return nullptr;
    }

    size_t image_size = pixel_size * args->info->width * args->info->height;
    if (args->memory_offset >= host_memory->size()) {
      error_reporter->ERROR()
          << "Image::CreateFromMemory(): the offset of the Image must be "
          << "within the range of the Memory";
      return nullptr;
    }

    if (args->memory_offset + image_size > host_memory->size()) {
      error_reporter->ERROR()
          << "Image::CreateFromMemory(): the Image must fit within the size "
          << "of the Memory";
      return nullptr;
    }

    auto escher_image = escher::image_utils::NewImageFromPixels(
        session->context()->escher_image_factory(),
        session->context()->escher_gpu_uploader(), pixel_format,
        args->info->width, args->info->height,
        (uint8_t*)host_memory->memory_base());
    return ftl::AdoptRef(new Image(session, escher_image, host_memory));

    // Create from GPU memory.
  } else if (memory->IsKindOf<GpuMemory>()) {
    auto gpu_memory = memory->As<GpuMemory>();

    escher::ImageInfo info;
    info.format = pixel_format;
    info.width = args->info->width;
    info.height = args->info->height;
    info.sample_count = 1;
    info.usage =
        vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
    info.memory_flags = vk::MemoryPropertyFlagBits::eDeviceLocal;

    vk::Device vk_device = session->context()->vk_device();
    vk::Image vk_image = escher::image_utils::CreateVkImage(vk_device, info);

    // Make sure that the image is within range of its associated memory.
    vk::MemoryRequirements memory_reqs;
    vk_device.getImageMemoryRequirements(vk_image, &memory_reqs);

    if (args->memory_offset >= gpu_memory->size()) {
      error_reporter->ERROR()
          << "Image::CreateFromMemory(): the offset of the Image must be "
          << "within the range of the Memory";
      return nullptr;
    }

    if (args->memory_offset + memory_reqs.size > gpu_memory->size()) {
      error_reporter->ERROR()
          << "Image::CreateFromMemory(): the Image must fit within the size "
          << "of the Memory";
      return nullptr;
    }

    vk::DeviceMemory vk_mem = gpu_memory->escher_gpu_mem()->base();
    VkDeviceSize offset = args->memory_offset;
    vk_device.bindImageMemory(vk_image, vk_mem, offset);
    return ftl::AdoptRef(new Image(session, info, vk_image, gpu_memory));
  } else {
    FTL_CHECK(false);
    return nullptr;
  }
}

}  // namespace scene
}  // namespace mozart
