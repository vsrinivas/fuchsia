// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_VK_VULKAN_CONTEXT_H_
#define LIB_ESCHER_VK_VULKAN_CONTEXT_H_

#include <cstdint>
#include <vulkan/vulkan.hpp>

namespace escher {

// Used by clients to provide Escher with the required Vulkan resources.
struct VulkanContext {
  const vk::Instance instance;
  const vk::PhysicalDevice physical_device;
  const vk::Device device;
  // Queue that supports both graphics and compute.
  const vk::Queue queue;
  const uint32_t queue_family_index;
  // Optional transfer-only queue that is used for fast GPU uploads/downloads.
  const vk::Queue transfer_queue;
  const uint32_t transfer_queue_family_index;

  VulkanContext(vk::Instance instance, vk::PhysicalDevice physical_device,
                vk::Device device, vk::Queue queue, uint32_t queue_family_index,
                vk::Queue transfer_queue, uint32_t transfer_queue_family_index)
      : instance(instance),
        physical_device(physical_device),
        device(device),
        queue(queue),
        queue_family_index(queue_family_index),
        transfer_queue(transfer_queue),
        transfer_queue_family_index(transfer_queue_family_index) {}

  VulkanContext()
      : queue_family_index(UINT32_MAX),
        transfer_queue_family_index(UINT32_MAX) {}
};

}  // namespace escher

#endif  // LIB_ESCHER_VK_VULKAN_CONTEXT_H_
