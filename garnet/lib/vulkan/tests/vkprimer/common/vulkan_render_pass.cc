// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vulkan_render_pass.h"

#include "utils.h"

VulkanRenderPass::VulkanRenderPass(std::shared_ptr<VulkanLogicalDevice> device,
                                   const VkFormat &swapchain_image_format)
    : initialized_(false),
      device_(device),
      swapchain_image_format_(swapchain_image_format) {}

VulkanRenderPass::~VulkanRenderPass() {
  if (initialized_) {
    vkDestroyRenderPass(device_->device(), render_pass_, nullptr);
  }
}

bool VulkanRenderPass::Init() {
  if (initialized_) {
    RTN_MSG(false, "VulkanRenderPass is already initialized.\n");
  }

  VkAttachmentDescription color_attachment = {
      .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      .format = swapchain_image_format_,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
  };

  VkAttachmentReference color_attachment_ref = {
      .attachment = 0,
      .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
  };

  VkSubpassDescription subpass = {
      .colorAttachmentCount = 1,
      .pColorAttachments = &color_attachment_ref,
      .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
  };

  VkRenderPassCreateInfo render_pass_info = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &color_attachment,
      .pSubpasses = &subpass,
      .subpassCount = 1,
  };

  auto err = vkCreateRenderPass(device_->device(), &render_pass_info, nullptr,
                                &render_pass_);
  if (VK_SUCCESS != err) {
    RTN_MSG(false, "VK Error: 0x%x - Failed to create render pass.\n", err);
  }

  initialized_ = true;
  return true;
}
