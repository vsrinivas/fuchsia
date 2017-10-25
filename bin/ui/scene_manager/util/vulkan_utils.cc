// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/util/vulkan_utils.h"

#include "lib/escher/vk/gpu_mem.h"

namespace scene_manager {

vk::SurfaceKHR CreateVulkanMagmaSurface(const vk::Instance& instance) {
  VkMagmaSurfaceCreateInfoKHR create_info = {
      .sType = VK_STRUCTURE_TYPE_MAGMA_SURFACE_CREATE_INFO_KHR,
      .pNext = nullptr,
  };
  VkSurfaceKHR surface;
  VkResult err =
      vkCreateMagmaSurfaceKHR(instance, &create_info, nullptr, &surface);
  FXL_CHECK(!err);
  return surface;
}

}  // namespace scene_manager
