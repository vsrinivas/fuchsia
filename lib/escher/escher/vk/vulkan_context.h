// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <vulkan/vulkan.hpp>

namespace escher {

struct VulkanContext {
  const vk::Instance instance;
  const vk::PhysicalDevice physical_device;
  const vk::Device device;
  const vk::Queue queue;
  const uint32_t queue_family_index;

  VulkanContext(vk::Instance instance,
                vk::PhysicalDevice physical_device,
                vk::Device device,
                vk::Queue queue,
                uint32_t queue_family_index)
      : instance(instance),
        physical_device(physical_device),
        device(device),
        queue(queue),
        queue_family_index(queue_family_index) {}

  VulkanContext() : queue_family_index(UINT32_MAX) {}
};

}  // namespace escher
