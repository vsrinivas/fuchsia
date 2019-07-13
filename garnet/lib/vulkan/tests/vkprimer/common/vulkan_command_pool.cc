// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vulkan_command_pool.h"

#include "utils.h"

VulkanCommandPool::VulkanCommandPool(std::shared_ptr<VulkanLogicalDevice> device,
                                     const vk::PhysicalDevice phys_device,
                                     const VkSurfaceKHR &surface)
    : initialized_(false), device_(device) {
  params_ = std::make_unique<SurfacePhysDeviceParams>(phys_device, surface);
}

bool VulkanCommandPool::Init() {
  if (initialized_ == true) {
    RTN_MSG(false, "VulkanCommandPool is already initialized.\n");
  }

  std::vector<uint32_t> graphics_queue_family_indices;
  if (!vkp::FindGraphicsQueueFamilies(params_->phys_device_, params_->surface_,
                                      &graphics_queue_family_indices)) {
    RTN_MSG(false, "No graphics queue families found.\n");
  }

  vk::CommandPoolCreateInfo info;
  info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
  info.queueFamilyIndex = graphics_queue_family_indices[0];

  auto rv = device_->device()->createCommandPoolUnique(info);
  command_pool_ = std::move(rv.value);
  if (vk::Result::eSuccess != rv.result) {
    RTN_MSG(false, "VK Error: 0x%x - Failed to create command pool.\n", rv.result);
  }

  params_.reset();
  initialized_ = true;
  return true;
}
