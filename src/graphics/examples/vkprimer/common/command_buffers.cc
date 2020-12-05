// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/examples/vkprimer/common/command_buffers.h"

#include "utils.h"

#include "vulkan/vulkan.hpp"

namespace vkp {

CommandBuffers::CommandBuffers(std::shared_ptr<vk::Device> device,
                               std::shared_ptr<vkp::CommandPool> vkp_command_pool,
                               const std::vector<vk::UniqueFramebuffer> &framebuffers,
                               const vk::Extent2D &extent, const vk::RenderPass &render_pass,
                               const vk::Pipeline &graphics_pipeline)
    : device_(device),
      vkp_command_pool_(vkp_command_pool),
      num_command_buffers_(framebuffers.size()) {
  params_ = std::make_unique<InitParams>(framebuffers, extent, render_pass, graphics_pipeline);
}

bool CommandBuffers::Alloc() {
  vk::CommandBufferAllocateInfo alloc_info;
  alloc_info.setCommandBufferCount(static_cast<uint32_t>(num_command_buffers_));
  alloc_info.setCommandPool(vkp_command_pool_->get());
  alloc_info.level = vk::CommandBufferLevel::ePrimary;
  auto [r_cmd_bufs, cmd_bufs] = device_->allocateCommandBuffersUnique(alloc_info);
  RTN_IF_VKH_ERR(false, r_cmd_bufs, "Failed to allocate command buffers.\n");
  command_buffers_ = std::move(cmd_bufs);
  allocated_ = true;

  // Only custom initialization is allowed if |Alloc()| has been called.
  initialized_ = true;
  return true;
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
  RTN_IF_MSG(false, allocated_,
             "CommandBuffers already allocated.  Custom intialization required.\n");
  RTN_IF_MSG(false, initialized_, "CommandBuffers already initialized.\n");
  RTN_IF_MSG(false, !device_, "Device must be initialized.\n");
  RTN_IF_MSG(false, !Alloc(), "Couldn't allocate command buffers.\n");

  vk::ClearValue clear_color;
  clear_color.color = std::array<float, 4>({0.5f, 0.0f, 0.5f, 1.0f});
  vk::RenderPassBeginInfo render_pass_info;
  render_pass_info.renderPass = params_->render_pass_;
  render_pass_info.renderArea = vk::Rect2D(0 /* offset */, params_->extent_);
  render_pass_info.clearValueCount = 1;
  render_pass_info.pClearValues = &clear_color;

  const vk::CommandBufferBeginInfo begin_info(vk::CommandBufferUsageFlagBits::eSimultaneousUse,
                                              nullptr /* pInheritanceInfo */);
  for (size_t i = 0; i < num_command_buffers_; ++i) {
    vk::CommandBuffer &command_buffer = command_buffers_[i].get();
    RTN_IF_VKH_ERR(false, command_buffer.begin(&begin_info), "Failed to begin command buffer.\n");
    render_pass_info.framebuffer = params_->framebuffers_[i].get();

    // Record commands to render pass.
    command_buffer.beginRenderPass(&render_pass_info, vk::SubpassContents::eInline);
    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, params_->graphics_pipeline_);
    command_buffer.draw(3 /* vertexCount */, 1 /* instanceCount */, 0 /* firstVertex */,
                        0 /* firstInstance */);
    command_buffer.endRenderPass();

    command_buffer.end();
  }

  params_.reset();
  initialized_ = true;
  return true;
}

}  // namespace vkp
