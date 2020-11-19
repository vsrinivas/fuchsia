// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_VULKAN_COMMAND_POOL_H_
#define SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_VULKAN_COMMAND_POOL_H_

#include "device.h"
#include "src/lib/fxl/macros.h"
#include "surface_phys_device_params.h"

#include <vulkan/vulkan.hpp>

class VulkanCommandPool {
 public:
  VulkanCommandPool(std::shared_ptr<vkp::Device> vkp_device, const vk::PhysicalDevice &phys_device,
                    const VkSurfaceKHR &surface);

  bool Init();
  const vk::UniqueCommandPool &command_pool() const { return command_pool_; }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(VulkanCommandPool);

  bool initialized_;
  std::shared_ptr<vkp::Device> vkp_device_;
  std::unique_ptr<SurfacePhysDeviceParams> params_;

  vk::UniqueCommandPool command_pool_;
};

#endif  // SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_VULKAN_COMMAND_POOL_H_
