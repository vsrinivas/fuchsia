// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_QUEUE_H_
#define GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_QUEUE_H_

#include <src/lib/fxl/macros.h>

#include <vector>

#include "surface_phys_device_params.h"
#include "vulkan/vulkan.h"

class VulkanQueue {
 public:
  VulkanQueue(const VkPhysicalDevice &phys_device, const VkSurfaceKHR &surface);
  ~VulkanQueue();

  bool Init();

  // Find indices for graphics queue families with present support.
  // If |queue_family_indices| is non-null, populate with matching indices.
  // Returns true if a graphics queue family with present support is found.
  static bool FindGraphicsQueueFamilies(
      VkPhysicalDevice phys_device, VkSurfaceKHR surface,
      std::vector<uint32_t> *queue_family_indices);

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(VulkanQueue);

  bool initialized_;
  std::unique_ptr<SurfacePhysDeviceParams> params_;
};

#endif  // GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_QUEUE_H_
