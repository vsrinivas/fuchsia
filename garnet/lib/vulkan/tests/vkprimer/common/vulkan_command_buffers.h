// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_COMMAND_BUFFERS_H_
#define GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_COMMAND_BUFFERS_H_

#include <src/lib/fxl/macros.h>

#include <memory>
#include <vector>

#include "vulkan/vulkan.h"
#include "vulkan_command_pool.h"
#include "vulkan_logical_device.h"

class VulkanCommandBuffers {
 public:
  VulkanCommandBuffers(std::shared_ptr<VulkanLogicalDevice> device,
                       std::shared_ptr<VulkanCommandPool> command_pool,
                       const std::vector<VkFramebuffer> &framebuffers,
                       const VkExtent2D &extent,
                       const VkRenderPass &render_pass,
                       const VkPipeline &graphics_pipeline);
  ~VulkanCommandBuffers();

  bool Init();
  const std::vector<VkCommandBuffer> &command_buffers() const {
    return command_buffers_;
  }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(VulkanCommandBuffers);

  bool initialized_;
  std::shared_ptr<VulkanLogicalDevice> device_;
  std::shared_ptr<VulkanCommandPool> command_pool_;

  struct InitParams {
    InitParams(const std::vector<VkFramebuffer> &framebuffers,
               const VkExtent2D &extent, const VkRenderPass &render_pass,
               const VkPipeline &graphics_pipeline);
    std::vector<VkFramebuffer> framebuffers_;
    VkExtent2D extent_;
    VkRenderPass render_pass_;
    VkPipeline graphics_pipeline_;
  };
  std::unique_ptr<InitParams> params_;

  std::vector<VkCommandBuffer> command_buffers_;
};

#endif  // GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_COMMAND_BUFFERS_H_
