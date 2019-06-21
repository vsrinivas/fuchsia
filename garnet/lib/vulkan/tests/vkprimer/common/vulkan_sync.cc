// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vulkan_sync.h"

#include "utils.h"

VulkanSync::VulkanSync(std::shared_ptr<VulkanLogicalDevice> device,
                       int max_frames_in_flight)
    : initialized_(false),
      device_(device),
      max_frames_in_flight_(max_frames_in_flight),
      in_flight_fences_(max_frames_in_flight_) {}

bool VulkanSync::Init() {
  if (initialized_) {
    RTN_MSG(false, "VulkanSync is already initialized.\n");
  }

  image_available_semaphores_.resize(max_frames_in_flight_);
  render_finished_semaphores_.resize(max_frames_in_flight_);

  VkSemaphoreCreateInfo semaphore_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
  };

  VkFenceCreateInfo fence_info = {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .flags = VK_FENCE_CREATE_SIGNALED_BIT,
  };

  const VkDevice& device = device_->device();
  VkResult err;
  for (uint32_t i = 0; i < max_frames_in_flight_; i++) {
    err = vkCreateSemaphore(device, &semaphore_info, nullptr,
                            &image_available_semaphores_[i]);
    if (VK_SUCCESS != err) {
      RTN_MSG(false, "VK Error: 0x%x - Failed to image available semaphore.\n",
              err);
    }
    err = vkCreateSemaphore(device, &semaphore_info, nullptr,
                            &render_finished_semaphores_[i]);
    if (VK_SUCCESS != err) {
      RTN_MSG(false, "VK Error: 0x%x - Failed to render finished semaphore.\n",
              err);
    }
    err = vkCreateFence(device, &fence_info, nullptr, &in_flight_fences_[i]);
    if (VK_SUCCESS != err) {
      RTN_MSG(false, "VK Error: 0x%x - Failed to create fence.\n", err);
    }
  }

  initialized_ = true;
  return true;
}

VulkanSync::~VulkanSync() {
  if (initialized_) {
    const VkDevice& device = device_->device();
    for (uint32_t i = 0; i < max_frames_in_flight_; i++) {
      vkDestroySemaphore(device, image_available_semaphores_[i], nullptr);
      vkDestroySemaphore(device, render_finished_semaphores_[i], nullptr);
      vkDestroyFence(device, in_flight_fences_[i], nullptr);
    }
  }
}

const std::vector<VkSemaphore>& VulkanSync::image_available_semaphores() const {
  return image_available_semaphores_;
}

const std::vector<VkFence>& VulkanSync::in_flight_fences() const {
  return in_flight_fences_;
}

const std::vector<VkSemaphore>& VulkanSync::render_finished_semaphores() const {
  return render_finished_semaphores_;
}
