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

ImagePtr NaiveImage::AdoptVkImage(ResourceManager* image_owner, ImageInfo info,
                                  vk::Image vk_image, GpuMemPtr mem) {
  TRACE_DURATION("gfx", "escher::NaiveImage::AdoptImage (from VkImage)");
  FXL_CHECK(vk_image);
  FXL_CHECK(mem);
  auto bind_result = image_owner->vk_device().bindImageMemory(
      vk_image, mem->base(), mem->offset());
  if (bind_result != vk::Result::eSuccess) {
    FXL_DLOG(ERROR) << "vkBindImageMemory failed: "
                    << vk::to_string(bind_result);
    return nullptr;
  }

  return fxl::AdoptRef(new NaiveImage(image_owner, info, vk_image, mem));
}

NaiveImage::NaiveImage(ResourceManager* image_owner, ImageInfo info,
                       vk::Image image, GpuMemPtr mem)
    : Image(image_owner, info, image, mem->size(), mem->mapped_ptr()),
      mem_(std::move(mem)) {}

NaiveImage::~NaiveImage() { vulkan_context().device.destroyImage(vk()); }

}  // namespace impl
}  // namespace escher
