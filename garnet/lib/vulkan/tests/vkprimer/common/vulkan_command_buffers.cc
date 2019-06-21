// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vulkan_command_buffers.h"

#include "utils.h"

VulkanCommandBuffers::VulkanCommandBuffers(
    std::shared_ptr<VulkanLogicalDevice> device,
    std::shared_ptr<VulkanCommandPool> command_pool,
    const std::vector<VkFramebuffer> &framebuffers, const VkExtent2D &extent,
    const VkRenderPass &render_pass, const VkPipeline &graphics_pipeline)
    : initialized_(false),
      device_(device),
      command_pool_(command_pool),
      command_buffers_(framebuffers.size()) {
  params_ = std::make_unique<InitParams>(framebuffers, extent, render_pass,
                                         graphics_pipeline);
}

VulkanCommandBuffers::~VulkanCommandBuffers() {
  if (initialized_) {
    vkFreeCommandBuffers(device_->device(), command_pool_->command_pool(),
                         (uint32_t)command_buffers_.size(),
                         command_buffers_.data());
  }
}

VulkanCommandBuffers::InitParams::InitParams(
    const std::vector<VkFramebuffer> &framebuffers, const VkExtent2D &extent,
    const VkRenderPass &render_pass, const VkPipeline &graphics_pipeline)
    : framebuffers_(framebuffers),
      extent_(extent),
      render_pass_(render_pass),
      graphics_pipeline_(graphics_pipeline) {}

bool VulkanCommandBuffers::Init() {
  if (initialized_) {
    RTN_MSG(false, "VulkanCommandBuffers already initialized.\n");
  }

  VkCommandBufferAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandBufferCount = (uint32_t)command_buffers_.size(),
      .commandPool = command_pool_->command_pool(),
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
  };

  auto err = vkAllocateCommandBuffers(device_->device(), &alloc_info,
                                      command_buffers_.data());
  if (VK_SUCCESS != err) {
    RTN_MSG(false, "VK Error: 0x%x - Failed to allocate command buffers.\n",
            err);
  }

  const VkClearValue kClearColor = {
      .color = {.float32 = {0.5f, 0.0f, 0.5f, 1.0f}}};

  for (size_t i = 0; i < command_buffers_.size(); i++) {
    const VkCommandBuffer &command_buffer = command_buffers_[i];

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
    };

    err = vkBeginCommandBuffer(command_buffer, &begin_info);
    if (VK_SUCCESS != err) {
      RTN_MSG(false,
              "VK Error: 0x%x - Failed to begin recording command buffer.\n",
              err);
    }

    VkRenderPassBeginInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = params_->render_pass_,
        .framebuffer = params_->framebuffers_[i],
        .renderArea.offset = {0, 0},
        .renderArea.extent = params_->extent_,
        .clearValueCount = 1,
        .pClearValues = &kClearColor,
    };

    // Record commands to render pass with vkCmd* calls.
    vkCmdBeginRenderPass(command_buffer, &render_pass_info,
                         VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      params_->graphics_pipeline_);

    vkCmdDraw(command_buffer, 3 /* vertexCount */, 1 /* instanceCount */,
              0 /* firstVertex */, 0 /* firstInstance */);

    vkCmdEndRenderPass(command_buffer);

    err = vkEndCommandBuffer(command_buffer);
    if (VK_SUCCESS != err) {
      RTN_MSG(false, "VK Error: 0x%x - Failed to record command buffer.\n",
              err);
    }
  }

  params_.reset();
  initialized_ = true;
  return true;
}
