// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_RENDER_PASS_H_
#define GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_RENDER_PASS_H_

#include <src/lib/fxl/macros.h>
#include <vulkan/vulkan.hpp>

#include "vulkan_logical_device.h"

class VulkanRenderPass {
 public:
  VulkanRenderPass(std::shared_ptr<VulkanLogicalDevice> device,
                   const vk::Format &swapchain_image_format);

  bool Init();
  const vk::UniqueRenderPass &render_pass() const { return render_pass_; }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(VulkanRenderPass);

  bool initialized_;
  std::shared_ptr<VulkanLogicalDevice> device_;
  const vk::Format swapchain_image_format_;
  vk::UniqueRenderPass render_pass_;
};

#endif  // GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_RENDER_PASS_H_
