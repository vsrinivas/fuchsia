// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vulkan_framebuffer.h"

#include "utils.h"

VulkanFramebuffer::VulkanFramebuffer(std::shared_ptr<VulkanLogicalDevice> device,
                                     std::shared_ptr<VulkanSwapchain> swap_chain,
                                     const vk::RenderPass &render_pass)
    : initialized_(false), device_(device), swap_chain_(swap_chain) {
  render_pass_ = std::make_unique<vk::RenderPass>(render_pass);
}

bool VulkanFramebuffer::Init() {
  if (initialized_) {
    RTN_MSG(false, "VulkanFramebuffer is already initialized.\n");
  }

  vk::Extent2D extent = swap_chain_->extent();
  const std::vector<vk::UniqueImageView> &swap_chain_image_views = swap_chain_->image_views();
  vk::FramebufferCreateInfo info;
  info.attachmentCount = 1;
  info.layers = 1;
  info.renderPass = *render_pass_;
  info.width = extent.width;
  info.height = extent.height;
  for (const auto &image_view : swap_chain_image_views) {
    info.setPAttachments(&(*image_view));
    auto rv = device_->device()->createFramebufferUnique(info);
    if (vk::Result::eSuccess != rv.result) {
      RTN_MSG(false, "VK Error: 0x%x - Failed to create framebuffer.\n", rv.result);
    }
    framebuffers_.emplace_back(std::move(rv.value));
  }

  initialized_ = true;
  return true;
}

const std::vector<vk::UniqueFramebuffer> &VulkanFramebuffer::framebuffers() const {
  return framebuffers_;
}
