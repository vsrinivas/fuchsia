// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/examples/vkproto/common/command_buffers.h"

#include "utils.h"

#include "vulkan/vulkan.hpp"

namespace vkp {

CommandBuffers::CommandBuffers(std::shared_ptr<vk::Device> device,
                               std::shared_ptr<vkp::CommandPool> vkp_command_pool,
                               const std::vector<vk::UniqueFramebuffer> &framebuffers,
                               const vk::Pipeline &graphics_pipeline,
                               const vk::RenderPass &render_pass, const vk::Extent2D &extent,
                               const std::array<float, 4> &clear_color,
                               const vk::CommandBufferUsageFlags &usage_flags,
                               const vk::CommandBufferLevel &level)
    : device_(device),
      vkp_command_pool_(std::move(vkp_command_pool)),
      framebuffers_(framebuffers),
      num_command_buffers_(framebuffers.size()),
      graphics_pipeline_(graphics_pipeline),
      render_pass_(render_pass),
      extent_(extent),
      clear_color_(clear_color),
      usage_flags_(usage_flags),
      level_(level) {}

bool CommandBuffers::Alloc() {
  vk::CommandBufferAllocateInfo alloc_info;
  alloc_info.setCommandBufferCount(static_cast<uint32_t>(num_command_buffers_));
  alloc_info.setCommandPool(vkp_command_pool_->get());
  alloc_info.level = level_;
  auto [r_cmd_bufs, cmd_bufs] = device_->allocateCommandBuffersUnique(alloc_info);
  RTN_IF_VKH_ERR(false, r_cmd_bufs, "Failed to allocate command buffers.\n");
  command_buffers_ = std::move(cmd_bufs);
  allocated_ = true;

  // Only custom initialization is allowed if |Alloc()| has been called.
  initialized_ = true;
  return true;
}

bool CommandBuffers::Init() {
  RTN_IF_MSG(false, allocated_,
             "CommandBuffers already allocated.  Custom intialization required.\n");
  RTN_IF_MSG(false, initialized_, "CommandBuffers already initialized.\n");
  RTN_IF_MSG(false, !device_, "Device must be initialized.\n");
  RTN_IF_MSG(false, !Alloc(), "Couldn't allocate command buffers.\n");

  vk::ClearValue clear_color;
  clear_color.color = clear_color_;
  vk::RenderPassBeginInfo render_pass_info;
  render_pass_info.renderPass = render_pass_;
  render_pass_info.renderArea = vk::Rect2D(0 /* offset */, extent_);
  render_pass_info.clearValueCount = 1;
  render_pass_info.pClearValues = &clear_color;

  const vk::CommandBufferBeginInfo begin_info(usage_flags_, nullptr /* pInheritanceInfo */);
  for (size_t i = 0; i < num_command_buffers_; ++i) {
    vk::CommandBuffer &command_buffer = command_buffers_[i].get();
    RTN_IF_VKH_ERR(false, command_buffer.begin(&begin_info), "Failed to begin command buffer.\n");
    render_pass_info.framebuffer = framebuffers_[i].get();
    // Record commands to render pass.
    command_buffer.beginRenderPass(&render_pass_info, vk::SubpassContents::eInline);
    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphics_pipeline_);
    command_buffer.draw(3 /* vertexCount */, 1 /* instanceCount */, 0 /* firstVertex */,
                        0 /* firstInstance */);
    command_buffer.endRenderPass();

    RTN_IF_VKH_ERR(false, command_buffer.end(), "Failed to end command buffer\n");
  }

  initialized_ = true;
  return true;
}

}  // namespace vkp
