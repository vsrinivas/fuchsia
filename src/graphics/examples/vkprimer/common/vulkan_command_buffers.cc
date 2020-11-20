// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vulkan_command_buffers.h"

#include "utils.h"

VulkanCommandBuffers::VulkanCommandBuffers(std::shared_ptr<vkp::Device> vkp_device,
                                           std::shared_ptr<VulkanCommandPool> command_pool,
                                           const VulkanFramebuffer &framebuffer,
                                           const vk::Extent2D &extent,
                                           const vk::RenderPass &render_pass,
                                           const vk::Pipeline &graphics_pipeline)
    : initialized_(false),
      vkp_device_(vkp_device),
      command_pool_(command_pool),
      command_buffers_(framebuffer.framebuffers().size()) {
  params_ = std::make_unique<InitParams>(framebuffer, extent, render_pass, graphics_pipeline);
}

VulkanCommandBuffers::InitParams::InitParams(const VulkanFramebuffer &framebuffer,
                                             const vk::Extent2D &extent,
                                             const vk::RenderPass &render_pass,
                                             const vk::Pipeline &graphics_pipeline)
    : framebuffer_(framebuffer),
      extent_(extent),
      render_pass_(render_pass),
      graphics_pipeline_(graphics_pipeline) {}

bool VulkanCommandBuffers::Init() {
  RTN_IF_MSG(false, initialized_, "VulkanCommandBuffers already initialized.\n");

  vk::CommandBufferAllocateInfo alloc_info;
  alloc_info.setCommandBufferCount(static_cast<uint32_t>(command_buffers_.size()));
  alloc_info.setCommandPool(*command_pool_->command_pool());
  alloc_info.level = vk::CommandBufferLevel::ePrimary;

  auto [r_cmd_bufs, cmd_bufs] = vkp_device_->get().allocateCommandBuffersUnique(alloc_info);
  RTN_IF_VKH_ERR(false, r_cmd_bufs, "Failed to allocate command buffers.\n");
  command_buffers_ = std::move(cmd_bufs);

  vk::ClearValue clear_color;
  clear_color.setColor(std::array<float, 4>({0.5f, 0.0f, 0.5f, 1.0f}));

  auto framebuffer_iter = params_->framebuffer_.framebuffers().begin();
  for (const auto &command_buffer : command_buffers_) {
    const auto &framebuffer = *(framebuffer_iter++);

    vk::CommandBufferBeginInfo begin_info;
    begin_info.flags = vk::CommandBufferUsageFlagBits::eSimultaneousUse;
    RTN_IF_VKH_ERR(false, command_buffer->begin(&begin_info), "Failed to begin command buffer.\n");

    vk::Rect2D render_area;
    render_area.extent = params_->extent_;

    vk::RenderPassBeginInfo render_pass_info;
    render_pass_info.renderPass = params_->render_pass_;
    render_pass_info.framebuffer = *framebuffer;
    render_pass_info.renderArea = render_area;
    render_pass_info.clearValueCount = 1;
    render_pass_info.pClearValues = &clear_color;

    // Record commands to render pass.
    command_buffer->beginRenderPass(&render_pass_info, vk::SubpassContents::eInline);
    command_buffer->bindPipeline(vk::PipelineBindPoint::eGraphics, params_->graphics_pipeline_);
    command_buffer->draw(3 /* vertexCount */, 1 /* instanceCount */, 0 /* firstVertex */,
                         0 /* firstInstance */);
    command_buffer->endRenderPass();

    if (image_for_foreign_transition_) {
      AddForeignTransitionImageBarriers(command_buffer);
    }
    command_buffer->end();
  }

  params_.reset();
  initialized_ = true;
  return true;
}

void VulkanCommandBuffers::AddForeignTransitionImageBarriers(
    const vk::UniqueCommandBuffer &command_buffer) {
  vk::ImageMemoryBarrier barrier;
  barrier.setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
      .setOldLayout(vk::ImageLayout::eTransferSrcOptimal)
      .setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
      .setSrcQueueFamilyIndex(queue_family_)
      .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_EXTERNAL)
      .setSubresourceRange(vk::ImageSubresourceRange()
                               .setAspectMask(vk::ImageAspectFlagBits::eColor)
                               .setLevelCount(1)
                               .setLayerCount(1))
      .setImage(image_for_foreign_transition_);

  command_buffer->pipelineBarrier(vk::PipelineStageFlagBits::eAllGraphics,
                                  vk::PipelineStageFlagBits::eAllGraphics, {}, {}, {}, {barrier});

  // This barrier should transition it back
  vk::ImageMemoryBarrier barrier2(barrier);
<<<<<<< HEAD
  barrier2.setSrcQueueFamilyIndex(barrier.dstQueueFamilyIndex);
  barrier2.setDstQueueFamilyIndex(barrier.srcQueueFamilyIndex);

=======
>>>>>>> 55655bf08bd ([vulkan] Refactor / modernize vkprimer.)
  command_buffer->pipelineBarrier(vk::PipelineStageFlagBits::eAllGraphics,
                                  vk::PipelineStageFlagBits::eAllGraphics, {}, {}, {}, {barrier2});
}

const std::vector<vk::UniqueCommandBuffer> &VulkanCommandBuffers::command_buffers() const {
  return command_buffers_;
}
