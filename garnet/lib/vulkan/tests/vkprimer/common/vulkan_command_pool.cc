// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vulkan_command_pool.h"

#include "utils.h"
#include "vulkan_queue.h"

VulkanCommandPool::VulkanCommandPool(
    std::shared_ptr<VulkanLogicalDevice> device,
    const VkPhysicalDevice phys_device, const VkSurfaceKHR &surface)
    : initialized_(false), device_(device) {
  params_ = std::make_unique<SurfacePhysDeviceParams>(phys_device, surface);
}

VulkanCommandPool::~VulkanCommandPool() {
  if (initialized_) {
    vkResetCommandPool(device_->device(), command_pool_,
                       VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
    vkDestroyCommandPool(device_->device(), command_pool_, nullptr);
  }
}

bool VulkanCommandPool::Init() {
  if (initialized_ == true) {
    RTN_MSG(false, "VulkanCommandPool is already initialized.\n");
  }

  std::vector<uint32_t> graphics_queue_family_indices;
  if (!VulkanQueue::FindGraphicsQueueFamilies(params_->phys_device_,
                                              params_->surface_,
                                              &graphics_queue_family_indices)) {
    RTN_MSG(false, "No graphics queue families found.\n");
  }

  VkCommandPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = graphics_queue_family_indices[0],
  };

  auto err = vkCreateCommandPool(device_->device(), &pool_info, nullptr,
                                 &command_pool_);
  if (VK_SUCCESS != err) {
    RTN_MSG(false, "VK Error: 0x%x - Failed to create command pool.\n", err);
  }

  params_.reset();
  initialized_ = true;
  return true;
}
