// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/forward_declarations.h"
#include "escher/vk/vulkan_swapchain.h"

namespace escher {

class VulkanSwapchainHelper {
 public:
  VulkanSwapchainHelper(VulkanSwapchain swapchain, const RendererPtr& renderer);
  ~VulkanSwapchainHelper();

  void DrawFrame(const Stage& stage, const Model& model, const Camera& camera);
  // This variant creates a default camera looking directly down at the center
  // of the Stage.
  // TODO: remove this variant; clients should always specify the Camera.
  void DrawFrame(const Stage& stage, const Model& model);

  const VulkanSwapchain& swapchain() const { return swapchain_; }

 private:
  VulkanSwapchain swapchain_;
  RendererPtr renderer_;
  vk::Device device_;
  vk::Queue queue_;

  size_t next_semaphore_index_ = 0;
  std::vector<SemaphorePtr> image_available_semaphores_;
  std::vector<SemaphorePtr> render_finished_semaphores_;

  FTL_DISALLOW_COPY_AND_ASSIGN(VulkanSwapchainHelper);
};

}  // namespace escher
