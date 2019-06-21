// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vulkan_queue.h"

#include <vector>

#include "utils.h"

VulkanQueue::VulkanQueue(const VkPhysicalDevice& phys_device,
                         const VkSurfaceKHR& surface)
    : initialized_(false) {
  params_ = std::make_unique<SurfacePhysDeviceParams>(phys_device, surface);
}

VulkanQueue::~VulkanQueue() {
  if (initialized_) {
    initialized_ = false;
  }
}

bool VulkanQueue::Init() {
  if (!FindGraphicsQueueFamilies(params_->phys_device_, params_->surface_,
                                 nullptr)) {
    RTN_MSG(false, "No graphics family queue found.\n");
  }
  params_.reset();
  initialized_ = true;
  return true;
}

bool VulkanQueue::FindGraphicsQueueFamilies(
    VkPhysicalDevice phys_device, VkSurfaceKHR surface,
    std::vector<uint32_t>* queue_family_indices) {
  uint32_t num_queue_families = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(phys_device, &num_queue_families,
                                           nullptr);
  std::vector<VkQueueFamilyProperties> queue_families(num_queue_families);
  vkGetPhysicalDeviceQueueFamilyProperties(phys_device, &num_queue_families,
                                           queue_families.data());

  int queue_family_index = 0;
  for (const auto& queue_family : queue_families) {
    VkBool32 present_support = false;
    vkGetPhysicalDeviceSurfaceSupportKHR(phys_device, queue_family_index,
                                         surface, &present_support);

    if (queue_family.queueCount > 0 &&
        queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT && present_support) {
      if (queue_family_indices) {
        queue_family_indices->emplace_back(queue_family_index);
      }
      return true;
    }
    queue_family_index++;
  }
  RTN_MSG(false, "No queue family indices found.\n");
}
