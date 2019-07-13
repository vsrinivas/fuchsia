// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_COMMAND_POOL_H_
#define GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_COMMAND_POOL_H_

#include <src/lib/fxl/macros.h>
#include <vulkan/vulkan.hpp>

#include "surface_phys_device_params.h"
#include "vulkan_logical_device.h"

class VulkanCommandPool {
 public:
  VulkanCommandPool(std::shared_ptr<VulkanLogicalDevice> device,
                    const vk::PhysicalDevice phys_device, const VkSurfaceKHR &surface);

  bool Init();
  const vk::UniqueCommandPool &command_pool() const { return command_pool_; }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(VulkanCommandPool);

  bool initialized_;
  std::shared_ptr<VulkanLogicalDevice> device_;
  std::unique_ptr<SurfacePhysDeviceParams> params_;

  vk::UniqueCommandPool command_pool_;
};

#endif  // GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_COMMAND_POOL_H_
