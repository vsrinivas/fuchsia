// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_VULKAN_RENDER_PASS_H_
#define SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_VULKAN_RENDER_PASS_H_

#include "src/lib/fxl/macros.h"
#include "vulkan_logical_device.h"

#include <vulkan/vulkan.hpp>

class VulkanRenderPass {
 public:
  VulkanRenderPass(std::shared_ptr<VulkanLogicalDevice> device, const vk::Format &image_format,
                   bool offscreen);

  void set_initial_layout(vk::ImageLayout initial_layout) { initial_layout_ = initial_layout; }

  bool Init();
  const vk::UniqueRenderPass &render_pass() const { return render_pass_; }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(VulkanRenderPass);

  bool initialized_;
  std::shared_ptr<VulkanLogicalDevice> device_;
  const vk::Format image_format_;
  bool offscreen_;
  vk::UniqueRenderPass render_pass_;
  vk::ImageLayout initial_layout_ = vk::ImageLayout::eUndefined;
};

#endif  // SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_VULKAN_RENDER_PASS_H_
