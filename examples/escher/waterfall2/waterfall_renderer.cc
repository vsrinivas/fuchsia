// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/escher/waterfall2/waterfall_renderer.h"

#include "lib/escher/escher.h"
#include "lib/escher/resources/resource_recycler.h"
#include "lib/escher/scene/model.h"
#include "lib/escher/util/trace_macros.h"
#include "lib/escher/vk/command_buffer.h"
#include "lib/escher/vk/image.h"
#include "lib/escher/vk/render_pass_info.h"
#include "lib/escher/vk/shader_program.h"
#include "lib/escher/vk/texture.h"

using namespace escher;

WaterfallRendererPtr WaterfallRenderer::New(EscherWeakPtr escher) {
  return fxl::AdoptRef(new WaterfallRenderer(std::move(escher)));
}

WaterfallRenderer::WaterfallRenderer(EscherWeakPtr weak_escher)
    : Renderer(weak_escher), render_queue_(std::move(weak_escher)) {
  // Need at least one.
  SetNumDepthBuffers(1);
}

WaterfallRenderer::~WaterfallRenderer() { escher()->Cleanup(); }

void WaterfallRenderer::DrawFrame(const escher::FramePtr& frame,
                                  escher::Stage* stage,
                                  const escher::Camera& camera,
                                  const escher::Stopwatch& stopwatch,
                                  uint64_t frame_count, Scene* scene,
                                  const escher::ImagePtr& output_image) {
  TRACE_DURATION("gfx", "WaterfallRenderer::DrawFrame");

  auto cb = frame->cmds();

  frame->command_buffer()->TakeWaitSemaphore(
      output_image, vk::PipelineStageFlagBits::eColorAttachmentOutput);

  render_queue_.InitFrame(frame, *stage, camera);
  scene->Update(stopwatch, frame_count, stage, &render_queue_);
  render_queue_.Sort();
  BeginRenderPass(frame, output_image);
  render_queue_.GenerateCommands(cb, nullptr);
  render_queue_.Clear();
  EndRenderPass(frame);
}

void WaterfallRenderer::BeginRenderPass(const escher::FramePtr& frame,
                                        const escher::ImagePtr& output_image) {
  TexturePtr depth_texture;
  {
    FXL_DCHECK(!depth_buffers_.empty());
    auto index = frame->frame_number() % depth_buffers_.size();
    depth_texture = depth_buffers_[index];
    if (!depth_texture || depth_texture->width() != output_image->width() ||
        depth_texture->height() != output_image->height()) {
      // Need to generate a new depth buffer.
      TRACE_DURATION("gfx",
                     "WaterfallRenderer::DrawFrame (create depth image)");
      depth_texture = escher()->NewAttachmentTexture(
          vk::Format::eD24UnormS8Uint, output_image->width(),
          output_image->height(), output_image->info().sample_count,
          vk::Filter::eLinear);
      depth_buffers_[index] = depth_texture;
    }
  }

  RenderPassInfo render_pass_info;
  {
    auto& rp = render_pass_info;
    rp.color_attachments[0] =
        ImageView::New(escher()->resource_recycler(), output_image);
    rp.num_color_attachments = 1;
    // Clear and store color attachment 0, the sole color attachment.
    rp.clear_attachments = 1u;
    rp.store_attachments = 1u;
    rp.depth_stencil_attachment = depth_texture;
    // Standard flags for a depth-testing render-pass that needs to first clear
    // the depth image.
    rp.op_flags = RenderPassInfo::kClearDepthStencilOp |
                  RenderPassInfo::kOptimalColorLayoutOp |
                  RenderPassInfo::kOptimalDepthStencilLayoutOp;
    rp.clear_color[0].setFloat32({0.f, 0.f, 0.2f, 1.f});
  }
  FXL_CHECK(render_pass_info.Validate());

  frame->cmds()->BeginRenderPass(render_pass_info);
  frame->AddTimestamp("started lighting render pass");
}

void WaterfallRenderer::EndRenderPass(const escher::FramePtr& frame) {
  frame->cmds()->EndRenderPass();
  frame->AddTimestamp("finished lighting render pass");
}

void WaterfallRenderer::SetNumDepthBuffers(size_t count) {
  FXL_DCHECK(count > 0);
  depth_buffers_.resize(count);
}
