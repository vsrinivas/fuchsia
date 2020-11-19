// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vulkan_render_pass.h"

#include "utils.h"

VulkanRenderPass::VulkanRenderPass(std::shared_ptr<vkp::Device> vkp_device,
                                   const vk::Format &image_format, bool offscreen)
    : initialized_(false),
      vkp_device_(vkp_device),
      image_format_(image_format),
      offscreen_(offscreen) {}

bool VulkanRenderPass::Init() {
  RTN_IF_MSG(false, initialized_, "VulkanRenderPass is already initialized.\n");

  vk::AttachmentDescription color_attachment;
  if (offscreen_) {
    color_attachment.finalLayout = vk::ImageLayout::eTransferSrcOptimal;
  } else {
    color_attachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;
  }
  color_attachment.format = image_format_;
  color_attachment.initialLayout = initial_layout_;
  color_attachment.loadOp = vk::AttachmentLoadOp::eClear;
  color_attachment.samples = vk::SampleCountFlagBits::e1;
  color_attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
  color_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
  color_attachment.storeOp = vk::AttachmentStoreOp::eStore;

  vk::AttachmentReference color_attachment_ref;
  color_attachment_ref.attachment = 0;
  color_attachment_ref.layout = vk::ImageLayout::eColorAttachmentOptimal;

  vk::SubpassDescription subpass;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_attachment_ref;
  subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;

  vk::RenderPassCreateInfo render_pass_info;
  render_pass_info.attachmentCount = 1;
  render_pass_info.pAttachments = &color_attachment;
  render_pass_info.pSubpasses = &subpass;
  render_pass_info.subpassCount = 1;

  auto [r_render_pass, render_pass] = vkp_device_->get().createRenderPassUnique(render_pass_info);
  RTN_IF_VKH_ERR(false, r_render_pass, "Failed to create render pass.\n");
  render_pass_ = std::move(render_pass);

  initialized_ = true;
  return true;
}
