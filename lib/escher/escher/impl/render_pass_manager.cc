// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/render_pass_manager.h"

#include "escher/impl/vulkan_utils.h"

namespace escher {
namespace impl {

RenderPassManager::RenderPassManager(const VulkanContext& context)
    : context_(context) {}

RenderPassManager::~RenderPassManager() {
  DestroyRenderPass(paper_renderer_render_pass_);
}

void RenderPassManager::DestroyRenderPass(vk::RenderPass render_pass) {
  if (render_pass) {
    context_.device.destroyRenderPass(render_pass);
  }
}

vk::RenderPass RenderPassManager::GetPaperRendererRenderPass() {
  if (!paper_renderer_render_pass_) {
    paper_renderer_render_pass_ = CreatePaperRendererRenderPass();
  }
  return paper_renderer_render_pass_;
}

vk::RenderPass RenderPassManager::CreatePaperRendererRenderPass() {
  std::vector<vk::AttachmentDescription> attachments(2);
  auto& color_attachment = attachments[0];
  auto& depth_attachment = attachments[1];

  // TODO: VulkanProvider should know the swapchain format and we should use it.
  color_attachment.format = vk::Format::eB8G8R8A8Unorm;
  color_attachment.samples = vk::SampleCountFlagBits::e1;
  color_attachment.loadOp = vk::AttachmentLoadOp::eClear;
  color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
  color_attachment.initialLayout = vk::ImageLayout::eUndefined;
  color_attachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;

  depth_attachment.format = ESCHER_CHECKED_VK_RESULT(
      GetSupportedDepthFormat(context_.physical_device));
  depth_attachment.samples = vk::SampleCountFlagBits::e1;
  depth_attachment.loadOp = vk::AttachmentLoadOp::eClear;
  depth_attachment.storeOp = vk::AttachmentStoreOp::eDontCare;
  depth_attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
  depth_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
  depth_attachment.initialLayout =
      vk::ImageLayout::eDepthStencilAttachmentOptimal;
  depth_attachment.finalLayout =
      vk::ImageLayout::eDepthStencilAttachmentOptimal;

  vk::AttachmentReference color_reference;
  color_reference.attachment = 0;
  color_reference.layout = vk::ImageLayout::eColorAttachmentOptimal;

  vk::AttachmentReference depth_reference;
  depth_reference.attachment = 1;
  depth_reference.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

  // We need at least one subpass.
  vk::SubpassDescription subpass;
  subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_reference;
  subpass.pDepthStencilAttachment = &depth_reference;
  subpass.inputAttachmentCount = 0;  // no other subpasses to sample from

  // Even though we have a single subpass, we need to declare dependencies to
  // support the layout transitions specified by the attachment references.
  std::vector<vk::SubpassDependency> dependencies(2);

  // The first dependency transitions from the final layout from the previous
  // render pass, to the initial layout of this one.
  dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;  // not in vulkan.hpp ?!?
  dependencies[0].dstSubpass = 0;
  dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
  dependencies[0].dstStageMask =
      vk::PipelineStageFlagBits::eColorAttachmentOutput;
  dependencies[0].srcAccessMask = vk::AccessFlagBits::eMemoryRead;
  dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead |
                                  vk::AccessFlagBits::eColorAttachmentWrite;
  dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;

  // The second dependency describes the transition from the initial to final
  // layout.
  dependencies[1].srcSubpass = 0;  // our sole subpass
  dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[1].srcStageMask =
      vk::PipelineStageFlagBits::eColorAttachmentOutput;
  dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
  dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentRead |
                                  vk::AccessFlagBits::eColorAttachmentWrite;
  dependencies[1].dstAccessMask = vk::AccessFlagBits::eMemoryRead;
  dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

  // Create the render pass, now that we can fully specify it.
  vk::RenderPassCreateInfo info;
  info.attachmentCount = static_cast<uint32_t>(attachments.size());
  info.pAttachments = attachments.data();
  info.subpassCount = 1;
  info.pSubpasses = &subpass;
  info.dependencyCount = static_cast<uint32_t>(dependencies.size());
  info.pDependencies = dependencies.data();

  return ESCHER_CHECKED_VK_RESULT(context_.device.createRenderPass(info));
}

}  // namespace impl
}  // namespace escher
