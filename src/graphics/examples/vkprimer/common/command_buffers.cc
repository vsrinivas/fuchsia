// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/examples/vkprimer/common/command_buffers.h"

#include "utils.h"

#include "vulkan/vulkan.hpp"

namespace vkp {

CommandBuffers::CommandBuffers(std::shared_ptr<Device> vkp_device,
                               std::shared_ptr<CommandPool> vkp_command_pool,
                               const std::vector<vk::UniqueFramebuffer> &framebuffers,
                               const vk::Extent2D &extent, const vk::RenderPass &render_pass,
                               const vk::Pipeline &graphics_pipeline)
    : initialized_(false),
      vkp_device_(std::move(vkp_device)),
      vkp_command_pool_(std::move(vkp_command_pool)),
      num_command_buffers_(framebuffers.size()) {
  params_ = std::make_unique<InitParams>(framebuffers, extent, render_pass, graphics_pipeline);
}

CommandBuffers::InitParams::InitParams(const std::vector<vk::UniqueFramebuffer> &framebuffers,
                                       const vk::Extent2D &extent,
                                       const vk::RenderPass &render_pass,
                                       const vk::Pipeline &graphics_pipeline)
    : framebuffers_(framebuffers),
      extent_(extent),
      render_pass_(render_pass),
      graphics_pipeline_(graphics_pipeline) {}

bool CommandBuffers::Init() {
  RTN_IF_MSG(false, initialized_, "CommandBuffers already initialized.\n");

  vk::CommandBufferAllocateInfo alloc_info;
  alloc_info.setCommandBufferCount(static_cast<uint32_t>(num_command_buffers_));
  alloc_info.setCommandPool(vkp_command_pool_->get());
  alloc_info.level = vk::CommandBufferLevel::ePrimary;
  auto [r_cmd_bufs, cmd_bufs] = vkp_device_->get().allocateCommandBuffersUnique(alloc_info);
  RTN_IF_VKH_ERR(false, r_cmd_bufs, "Failed to allocate command buffers.\n");
  command_buffers_ = std::move(cmd_bufs);

  vk::ClearValue clear_color;
  clear_color.color = std::array<float, 4>({0.5f, 0.0f, 0.5f, 1.0f});

  for (size_t i = 0; i < num_command_buffers_; ++i) {
    vk::CommandBuffer &command_buffer = command_buffers_[i].get();
    vk::CommandBufferBeginInfo begin_info;
    begin_info.flags = vk::CommandBufferUsageFlagBits::eSimultaneousUse;
    RTN_IF_VKH_ERR(false, command_buffer.begin(&begin_info), "Failed to begin command buffer.\n");

    vk::RenderPassBeginInfo render_pass_info;
    render_pass_info.renderPass = params_->render_pass_;
    render_pass_info.framebuffer = params_->framebuffers_[i].get();
    render_pass_info.renderArea = vk::Rect2D(0 /* offset */, params_->extent_);
    render_pass_info.clearValueCount = 1;
    render_pass_info.pClearValues = &clear_color;

    // Record commands to render pass.
    command_buffer.beginRenderPass(&render_pass_info, vk::SubpassContents::eInline);
    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, params_->graphics_pipeline_);
    command_buffer.draw(3 /* vertexCount */, 1 /* instanceCount */, 0 /* firstVertex */,
                        0 /* firstInstance */);
    command_buffer.endRenderPass();

    if (image_for_foreign_transition_) {
      AddForeignTransitionImageBarriers(command_buffer);
    }
    command_buffer.end();
  }

  params_.reset();
  initialized_ = true;
  return true;
}

void CommandBuffers::AddForeignTransitionImageBarriers(const vk::CommandBuffer &command_buffer) {
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

  command_buffer.pipelineBarrier(
      vk::PipelineStageFlagBits::eAllGraphics, vk::PipelineStageFlagBits::eAllGraphics,
      vk::DependencyFlags{}, 0 /* memoryBarrierCount */, nullptr /* memoryBarriers */,
      0 /* bufferMemoryBarrierCount */, nullptr /* bufferMemoryBarriers */,
      1 /* imageMemoryBarrierCount */, &barrier);

  // This barrier should transition it back
  vk::ImageMemoryBarrier barrier2(barrier);
  command_buffer.pipelineBarrier(
      vk::PipelineStageFlagBits::eAllGraphics, vk::PipelineStageFlagBits::eAllGraphics,
      vk::DependencyFlags{}, 0 /* memoryBarrierCount */, nullptr /* memoryBarriers */,
      0 /* bufferMemoryBarrierCount */, nullptr /* bufferMemoryBarriers */,
      1 /* imageMemoryBarrierCount */, &barrier2);
}

}  // namespace vkp
