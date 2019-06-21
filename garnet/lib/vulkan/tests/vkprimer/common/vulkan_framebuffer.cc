// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vulkan_framebuffer.h"

#include "utils.h"

VulkanFramebuffer::VulkanFramebuffer(
    std::shared_ptr<VulkanLogicalDevice> device,
    const std::vector<VkImageView> &swap_chain_image_views,
    const VkExtent2D &extent, const VkRenderPass &render_pass)
    : initialized_(false),
      device_(device),
      framebuffers_(swap_chain_image_views.size()) {
  params_ =
      std::make_unique<InitParams>(swap_chain_image_views, extent, render_pass);
}

VulkanFramebuffer::~VulkanFramebuffer() {
  if (initialized_) {
    for (const auto &framebuffer : framebuffers_) {
      vkDestroyFramebuffer(device_->device(), framebuffer, nullptr);
    }
  }
}

VulkanFramebuffer::InitParams::InitParams(
    const std::vector<VkImageView> &swap_chain_image_views,
    const VkExtent2D &extent, const VkRenderPass &render_pass)
    : swap_chain_image_views_(swap_chain_image_views),
      extent_(extent),
      render_pass_(render_pass) {}

bool VulkanFramebuffer::Init() {
  if (initialized_) {
    RTN_MSG(false, "VulkanFramebuffer is already initialized.\n");
  }

  for (size_t i = 0; i < params_->swap_chain_image_views_.size(); i++) {
    VkFramebufferCreateInfo framebuffer_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .attachmentCount = 1,
        .layers = 1,
        .pAttachments = &params_->swap_chain_image_views_[i],
        .renderPass = params_->render_pass_,
        .width = params_->extent_.width,
        .height = params_->extent_.height,
    };

    auto err = vkCreateFramebuffer(device_->device(), &framebuffer_info,
                                   nullptr, &framebuffers_[i]);
    if (VK_SUCCESS != err) {
      RTN_MSG(false, "VK Error: 0x%x - Failed to create framebuffer.\n", err);
    }
  }

  params_.reset();
  initialized_ = true;
  return true;
}
