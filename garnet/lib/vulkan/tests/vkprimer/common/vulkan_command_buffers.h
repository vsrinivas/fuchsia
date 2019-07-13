// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_COMMAND_BUFFERS_H_
#define GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_COMMAND_BUFFERS_H_

#include <src/lib/fxl/macros.h>

#include <memory>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "vulkan_command_pool.h"
#include "vulkan_framebuffer.h"
#include "vulkan_logical_device.h"

class VulkanCommandBuffers {
 public:
  VulkanCommandBuffers(std::shared_ptr<VulkanLogicalDevice> device,
                       std::shared_ptr<VulkanCommandPool> command_pool,
                       const VulkanFramebuffer &framebuffer, const vk::Extent2D &extent,
                       const vk::RenderPass &render_pass, const vk::Pipeline &graphics_pipeline);

  bool Init();
  const std::vector<vk::UniqueCommandBuffer> &command_buffers() const;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(VulkanCommandBuffers);

  bool initialized_;
  std::shared_ptr<VulkanLogicalDevice> device_;
  std::shared_ptr<VulkanCommandPool> command_pool_;

  struct InitParams {
    InitParams(const VulkanFramebuffer &framebuffer, const vk::Extent2D &extent,
               const vk::RenderPass &render_pass, const vk::Pipeline &graphics_pipeline);
    const VulkanFramebuffer &framebuffer_;
    vk::Extent2D extent_;
    vk::RenderPass render_pass_;
    vk::Pipeline graphics_pipeline_;
  };
  std::unique_ptr<InitParams> params_;
  std::vector<vk::UniqueCommandBuffer> command_buffers_;
};

#endif  // GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_COMMAND_BUFFERS_H_
