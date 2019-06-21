// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_FRAMEBUFFER_H_
#define GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_FRAMEBUFFER_H_

#include <src/lib/fxl/macros.h>

#include <vector>

#include "vulkan/vulkan.h"
#include "vulkan_logical_device.h"

class VulkanFramebuffer {
 public:
  VulkanFramebuffer(std::shared_ptr<VulkanLogicalDevice> device,
                    const std::vector<VkImageView> &swap_chain_image_views,
                    const VkExtent2D &extent, const VkRenderPass &render_pass);
  ~VulkanFramebuffer();

  bool Init();
  std::vector<VkFramebuffer> framebuffers() const { return framebuffers_; }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(VulkanFramebuffer);

  bool initialized_;
  std::shared_ptr<VulkanLogicalDevice> device_;

  struct InitParams {
    InitParams(const std::vector<VkImageView> &swap_chain_image_views,
               const VkExtent2D &extent, const VkRenderPass &render_pass);
    const std::vector<VkImageView> swap_chain_image_views_;
    const VkExtent2D extent_;
    const VkRenderPass render_pass_;
  };
  std::unique_ptr<InitParams> params_;

  std::vector<VkFramebuffer> framebuffers_;
};

#endif  // GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_FRAMEBUFFER_H_
