// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_VULKAN_FRAMEBUFFER_H_
#define SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_VULKAN_FRAMEBUFFER_H_

#include <vector>

#include "src/lib/fxl/macros.h"
#include "vulkan_logical_device.h"

#include <vulkan/vulkan.hpp>

class VulkanFramebuffer {
 public:
  VulkanFramebuffer(std::shared_ptr<VulkanLogicalDevice> device, const vk::Extent2D &extent,
                    const vk::RenderPass &render_pass,
                    const std::vector<vk::ImageView> &image_views);
  bool Init();
  const std::vector<vk::UniqueFramebuffer> &framebuffers() const;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(VulkanFramebuffer);

  bool initialized_;
  std::shared_ptr<VulkanLogicalDevice> device_;
  vk::Extent2D extent_;
  std::vector<vk::ImageView> image_views_;
  std::unique_ptr<vk::RenderPass> render_pass_;
  std::vector<vk::UniqueFramebuffer> framebuffers_;
};

#endif  // SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_VULKAN_FRAMEBUFFER_H_
