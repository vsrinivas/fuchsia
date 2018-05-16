// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/vk/image.h"

#include "lib/escher/impl/vulkan_utils.h"
#include "lib/escher/resources/resource_manager.h"
#include "lib/escher/util/image_utils.h"
#include "lib/escher/vk/gpu_allocator.h"
#include "lib/escher/vk/gpu_mem.h"

namespace escher {

const ResourceTypeInfo Image::kTypeInfo("Image", ResourceType::kResource,
                                        ResourceType::kWaitableResource,
                                        ResourceType::kImage);

ImagePtr Image::New(ResourceManager* image_owner, ImageInfo info,
                    vk::Image vk_image, GpuMemPtr mem,
                    vk::DeviceSize mem_offset, bool bind_image_memory) {
  if (mem && bind_image_memory) {
    auto bind_result = image_owner->vk_device().bindImageMemory(
        vk_image, mem->base(), mem->offset() + mem_offset);
    if (bind_result != vk::Result::eSuccess) {
      FXL_DLOG(ERROR) << "vkBindImageMemory failed: "
                      << vk::to_string(bind_result);
      return nullptr;
    }
  }
  return fxl::AdoptRef(new Image(image_owner, info, vk_image, mem, mem_offset));
}

ImagePtr Image::New(ResourceManager* image_owner, const ImageInfo& info,
                    GpuAllocator* allocator) {
  auto vk_device = image_owner->vk_device();
  vk::Image image = image_utils::CreateVkImage(vk_device, info);
  vk::MemoryRequirements reqs = vk_device.getImageMemoryRequirements(image);
  return Image::New(image_owner, info, image,
                    allocator->Allocate(reqs, info.memory_flags));
}

Image::Image(ResourceManager* image_owner, ImageInfo info, vk::Image vk_image,
             GpuMemPtr mem, vk::DeviceSize mem_offset)
    : WaitableResource(image_owner),
      info_(info),
      image_(vk_image),
      mem_(std::move(mem)),
      mem_offset_(mem_offset) {
  auto is_depth_stencil = image_utils::IsDepthStencilFormat(info.format);
  has_depth_ = is_depth_stencil.first;
  has_stencil_ = is_depth_stencil.second;
}

Image::~Image() {
  if (!mem_) {
    // Probably a swapchain image.  We don't own the image or the memory.
    FXL_LOG(INFO) << "Destroying Image with unowned VkImage (perhaps a "
                     "swapchain image?)";
  } else {
    vulkan_context().device.destroyImage(image_);
  }
}

}  // namespace escher
