// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_FRAMEBUFFER_H_
#define GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_FRAMEBUFFER_H_

#include <src/lib/fxl/macros.h>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "vulkan_logical_device.h"
#include "vulkan_swapchain.h"

class VulkanFramebuffer {
 public:
  VulkanFramebuffer(std::shared_ptr<VulkanLogicalDevice> device,
                    std::shared_ptr<VulkanSwapchain> swap_chain, const vk::RenderPass &render_pass);
  bool Init();
  const std::vector<vk::UniqueFramebuffer> &framebuffers() const;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(VulkanFramebuffer);

  bool initialized_;
  std::shared_ptr<VulkanLogicalDevice> device_;
  std::shared_ptr<VulkanSwapchain> swap_chain_;
  std::unique_ptr<vk::RenderPass> render_pass_;
  std::vector<vk::UniqueFramebuffer> framebuffers_;
};

#endif  // GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_FRAMEBUFFER_H_
