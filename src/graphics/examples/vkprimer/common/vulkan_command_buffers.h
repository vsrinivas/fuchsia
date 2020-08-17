// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_VULKAN_COMMAND_BUFFERS_H_
#define SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_VULKAN_COMMAND_BUFFERS_H_

#include <memory>
#include <vector>

#include "src/lib/fxl/macros.h"
#include "vulkan_command_pool.h"
#include "vulkan_framebuffer.h"
#include "vulkan_logical_device.h"

#include <vulkan/vulkan.hpp>

class VulkanCommandBuffers {
 public:
  VulkanCommandBuffers(std::shared_ptr<VulkanLogicalDevice> device,
                       std::shared_ptr<VulkanCommandPool> command_pool,
                       const VulkanFramebuffer &framebuffer, const vk::Extent2D &extent,
                       const vk::RenderPass &render_pass, const vk::Pipeline &graphics_pipeline);

  void set_image_for_foreign_transition(vk::Image image) { image_for_foreign_transition_ = image; }

  void set_queue_family(uint32_t queue_family) { queue_family_ = queue_family; }

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
  vk::Image image_for_foreign_transition_;
  uint32_t queue_family_{};
};

#endif  // SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_VULKAN_COMMAND_BUFFERS_H_
