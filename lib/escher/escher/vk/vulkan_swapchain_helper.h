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

  void DrawFrame(Stage& stage, Model& model);

 private:
  VulkanSwapchain swapchain_;
  RendererPtr renderer_;
  vk::Device device_;
  vk::Queue queue_;
  std::vector<FramebufferPtr> framebuffers_;
  SemaphorePtr image_available_semaphore_;
  SemaphorePtr render_finished_semaphore_;

  FTL_DISALLOW_COPY_AND_ASSIGN(VulkanSwapchainHelper);
};

}  // namespace escher
