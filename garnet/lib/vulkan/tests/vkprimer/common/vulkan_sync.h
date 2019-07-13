// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_SYNC_H_
#define GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_SYNC_H_

#include <src/lib/fxl/macros.h>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "vulkan_logical_device.h"

class VulkanSync {
 public:
  VulkanSync(std::shared_ptr<VulkanLogicalDevice> device, const int max_frames_in_flight);

  bool Init();

  const std::vector<vk::UniqueSemaphore>& image_available_semaphores() const;
  const std::vector<vk::UniqueFence>& in_flight_fences() const;
  const std::vector<vk::UniqueSemaphore>& render_finished_semaphores() const;
  int max_frames_in_flight() const { return max_frames_in_flight_; }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(VulkanSync);

  bool initialized_;
  std::shared_ptr<VulkanLogicalDevice> device_;
  const uint32_t max_frames_in_flight_;

  std::vector<vk::UniqueSemaphore> image_available_semaphores_;
  std::vector<vk::UniqueSemaphore> render_finished_semaphores_;
  std::vector<vk::UniqueFence> in_flight_fences_;
};

#endif  // GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_SYNC_H_
