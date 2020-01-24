// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vulkan_sync.h"

#include "utils.h"

VulkanSync::VulkanSync(std::shared_ptr<VulkanLogicalDevice> device, int max_frames_in_flight)
    : initialized_(false), device_(device), max_frames_in_flight_(max_frames_in_flight) {}

bool VulkanSync::Init() {
  if (initialized_) {
    RTN_MSG(false, "VulkanSync is already initialized.\n");
  }

  vk::SemaphoreCreateInfo semaphore_info;
  vk::FenceCreateInfo fence_info;
  fence_info.flags = vk::FenceCreateFlagBits::eSignaled;

  const vk::Device& device = *device_->device();
  for (uint32_t i = 0; i < max_frames_in_flight_; i++) {
    // Swap Chain Image Available Semaphore
    auto rv_semaphore = device.createSemaphoreUnique(semaphore_info);
    if (vk::Result::eSuccess != rv_semaphore.result) {
      RTN_MSG(false, "VK Error: 0x%x - Failed to image available semaphore.\n",
              rv_semaphore.result);
    }
    image_available_semaphores_.emplace_back(std::move(rv_semaphore.value));

    // Render Finished Semaphore
    rv_semaphore = device.createSemaphoreUnique(semaphore_info);
    if (vk::Result::eSuccess != rv_semaphore.result) {
      RTN_MSG(false, "VK Error: 0x%x - Failed to render finished semaphore.\n",
              rv_semaphore.result);
    }
    render_finished_semaphores_.emplace_back(std::move(rv_semaphore.value));

    // In Flight Fence
    auto rv_fence = device.createFenceUnique(fence_info);
    if (vk::Result::eSuccess != rv_fence.result) {
      RTN_MSG(false, "VK Error: 0x%x - Failed to create fence.\n", rv_fence.result);
    }
    in_flight_fences_.emplace_back(std::move(rv_fence.value));
  }

  initialized_ = true;
  return true;
}

const std::vector<vk::UniqueSemaphore>& VulkanSync::image_available_semaphores() const {
  return image_available_semaphores_;
}

const std::vector<vk::UniqueFence>& VulkanSync::in_flight_fences() const {
  return in_flight_fences_;
}

const std::vector<vk::UniqueSemaphore>& VulkanSync::render_finished_semaphores() const {
  return render_finished_semaphores_;
}
