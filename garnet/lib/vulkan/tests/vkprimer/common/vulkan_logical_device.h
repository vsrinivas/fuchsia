// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_LOGICAL_DEVICE_H_
#define GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_LOGICAL_DEVICE_H_

#include <src/lib/fxl/macros.h>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "surface_phys_device_params.h"

class VulkanLogicalDevice {
 public:
  VulkanLogicalDevice(const vk::PhysicalDevice &phys_device, const VkSurfaceKHR &surface,
                      const bool enabled_validation);

  bool Init();
  const vk::UniqueDevice &device() const;
  vk::Queue queue() const;

 private:
  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(VulkanLogicalDevice);

  bool AssignSuitableDevice(const std::vector<VkDevice> &devices);

  bool initialized_;
  std::unique_ptr<SurfacePhysDeviceParams> params_;
  const bool enable_validation_;
  std::vector<const char *> layers_;

  // Queue with support for both drawing and presentation.
  vk::Queue queue_;

  vk::UniqueDevice device_;
};

#endif  // GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_LOGICAL_DEVICE_H_
