// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/temp_frame_renderer.h"
#include "escher/impl/vulkan_utils.h"

namespace escher {
namespace impl {

TempFrameRenderer::TempFrameRenderer(const VulkanContext& context,
                                     vk::RenderPass render_pass)
    : context_(context), render_pass_(render_pass) {}

TempFrameRenderer::~TempFrameRenderer() {}

vk::Result TempFrameRenderer::Render(RenderContext::Frame* frame,
                                     vk::Framebuffer framebuffer) {
  FTL_LOG(INFO) << "rendering frame #" << frame->frame_number();

  auto result =
      frame->AllocateCommandBuffers(1, vk::CommandBufferLevel::ePrimary);
  if (result.result != vk::Result::eSuccess) {
    FTL_LOG(WARNING) << "failed to allocated CommandBuffer : "
                     << to_string(result.result);
    return result.result;
  }
  auto command_buffer = result.value[0];

  vk::ClearValue clear_values[2];
  clear_values[0] =
      vk::ClearColorValue(std::array<float, 4>{{0.f, 1.f, 0.f, 1.f}});
  clear_values[1] = vk::ClearDepthStencilValue{1.f, 0};

  static constexpr uint32_t kWidth = 1024;
  static constexpr uint32_t kHeight = 1024;

  vk::RenderPassBeginInfo render_pass_begin;
  render_pass_begin.renderPass = render_pass_;
  render_pass_begin.renderArea.offset.x = 0;
  render_pass_begin.renderArea.offset.y = 0;
  // TODO: pull these from somewhere
  render_pass_begin.renderArea.extent.width = kWidth;
  render_pass_begin.renderArea.extent.height = kHeight;
  render_pass_begin.clearValueCount = 2;
  render_pass_begin.pClearValues = clear_values;
  render_pass_begin.framebuffer = framebuffer;

  command_buffer.beginRenderPass(&render_pass_begin,
                                 vk::SubpassContents::eInline);

  vk::Viewport viewport;
  viewport.width = static_cast<float>(kWidth);
  viewport.height = static_cast<float>(kHeight);
  viewport.minDepth = static_cast<float>(0.0f);
  viewport.maxDepth = static_cast<float>(1.0f);
  command_buffer.setViewport(0, 1, &viewport);

  vk::Rect2D scissor;
  scissor.extent.width = kWidth;
  scissor.extent.height = kHeight;
  scissor.offset.x = 0;
  scissor.offset.y = 0;
  command_buffer.setScissor(0, 1, &scissor);

  command_buffer.endRenderPass();
  return command_buffer.end();
}

}  // namespace impl
}  // namespace escher
