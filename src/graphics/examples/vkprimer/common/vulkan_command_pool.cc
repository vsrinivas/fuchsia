// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vulkan_command_pool.h"

#include "utils.h"

VulkanCommandPool::VulkanCommandPool(std::shared_ptr<vkp::Device> vkp_device,
                                     const vk::PhysicalDevice &phys_device,
                                     const VkSurfaceKHR &surface)
    : initialized_(false), vkp_device_(vkp_device) {
  params_ = std::make_unique<SurfacePhysDeviceParams>(phys_device, surface);
}

bool VulkanCommandPool::Init() {
  RTN_IF_MSG(false, initialized_, "VulkanCommandPool is already initialized.\n");

  std::vector<uint32_t> graphics_queue_family_indices;
  if (!vkp::FindGraphicsQueueFamilies(params_->phys_device_, params_->surface_,
                                      &graphics_queue_family_indices)) {
    RTN_MSG(false, "No graphics queue families found.\n");
  }

  vk::CommandPoolCreateInfo info;
  info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
  info.queueFamilyIndex = graphics_queue_family_indices[0];

  auto [r_command_pool, command_pool] = vkp_device_->get().createCommandPoolUnique(info);
  RTN_IF_VKH_ERR(false, r_command_pool, "Failed to create command pool.\n");
  command_pool_ = std::move(command_pool);

  params_.reset();
  initialized_ = true;
  return true;
}
