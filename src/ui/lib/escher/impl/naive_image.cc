// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/impl/naive_image.h"

#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/resources/resource_manager.h"
#include "src/ui/lib/escher/util/trace_macros.h"
#include "src/ui/lib/escher/vk/gpu_mem.h"

namespace escher {
namespace impl {

ImagePtr NaiveImage::AdoptVkImage(ResourceManager* image_owner, ImageInfo info, vk::Image vk_image,
                                  GpuMemPtr mem) {
  TRACE_DURATION("gfx", "escher::NaiveImage::AdoptImage (from VkImage)");
  FXL_CHECK(vk_image);
  FXL_CHECK(mem);

  // Check image memory requirements before binding the image to memory.
  auto mem_requirements = image_owner->vk_device()
                              .getImageMemoryRequirements2KHR<vk::MemoryRequirements2KHR,
                                                              vk::MemoryDedicatedRequirementsKHR>(
                                  vk_image, image_owner->vulkan_context().loader);

  auto size_required = mem_requirements.get<vk::MemoryRequirements2KHR>().memoryRequirements.size;
  auto alignment_required =
      mem_requirements.get<vk::MemoryRequirements2KHR>().memoryRequirements.alignment;

  if (mem->size() < size_required) {
    FXL_LOG(ERROR) << "AdoptVkImage failed: Image requires " << size_required
                   << " bytes of memory, while the provided mem size is " << mem->size()
                   << " bytes.";
    return nullptr;
  }

  if (mem->offset() % alignment_required != 0) {
    FXL_LOG(ERROR) << "Memory requirements check failed: Buffer requires alignment of "
                   << alignment_required << " bytes, while the provided mem offset is "
                   << mem->offset();
    return nullptr;
  }

  auto bind_result = image_owner->vk_device().bindImageMemory(vk_image, mem->base(), mem->offset());
  if (bind_result != vk::Result::eSuccess) {
    FXL_DLOG(ERROR) << "vkBindImageMemory failed: " << vk::to_string(bind_result);
    return nullptr;
  }

  return fxl::AdoptRef(new NaiveImage(image_owner, info, vk_image, mem));
}

NaiveImage::NaiveImage(ResourceManager* image_owner, ImageInfo info, vk::Image image, GpuMemPtr mem)
    : Image(image_owner, info, image, mem->size(), mem->mapped_ptr()), mem_(std::move(mem)) {}

NaiveImage::~NaiveImage() { vulkan_context().device.destroyImage(vk()); }

}  // namespace impl
}  // namespace escher
