// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_LOGICAL_DEVICE_H_
#define GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_LOGICAL_DEVICE_H_

#include <src/lib/fxl/macros.h>

#include <vector>

#include "surface_phys_device_params.h"
#include "vulkan/vulkan.h"

class VulkanLogicalDevice {
 public:
  VulkanLogicalDevice(const VkPhysicalDevice &phys_device,
                      const VkSurfaceKHR &surface,
                      const bool enabled_validation);

  ~VulkanLogicalDevice();

  bool Init();
  VkDevice device() const;
  VkQueue queue() const;

 private:
  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(VulkanLogicalDevice);

  bool AssignSuitableDevice(const std::vector<VkDevice> &devices);

  bool initialized_;
  VkDevice device_;
  std::unique_ptr<SurfacePhysDeviceParams> params_;
  const bool enable_validation_;
  std::vector<const char *> layers_;

  // Queue with support for both drawing and presentation.
  VkQueue queue_;
};

#endif  // GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_LOGICAL_DEVICE_H_
