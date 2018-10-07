// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/paper/paper_renderer2.h"

#include "lib/escher/escher.h"
#include "lib/escher/paper/paper_renderer_config.h"
#include "lib/escher/renderer/batch_gpu_uploader.h"
#include "lib/escher/util/trace_macros.h"
#include "lib/escher/vk/command_buffer.h"
#include "lib/escher/vk/image.h"
#include "lib/escher/vk/render_pass_info.h"
#include "lib/escher/vk/shader_program.h"
#include "lib/escher/vk/texture.h"

namespace escher {

PaperRenderer2Ptr PaperRenderer2::New(EscherWeakPtr escher) {
  return fxl::AdoptRef(new PaperRenderer2(std::move(escher)));
}

PaperRenderer2::PaperRenderer2(EscherWeakPtr weak_escher)
    : Renderer(weak_escher),
      shape_cache_(
          weak_escher,
          PaperRendererConfig{.shadow_type = PaperRendererShadowType::kNone}),
      render_queue_(std::move(weak_escher)) {
  // Need at least one.
  SetNumDepthBuffers(1);
}

PaperRenderer2::~PaperRenderer2() { escher()->Cleanup(); }

PaperRenderer2::FrameData::FrameData(const FramePtr& frame_in,
                                     const ImagePtr& output_image_in)
    : frame(frame_in),
      output_image(output_image_in),
      gpu_uploader(BatchGpuUploader::New(frame->escher()->GetWeakPtr())) {}

PaperRenderer2::FrameData::~FrameData() = default;

void PaperRenderer2::BeginFrame(const FramePtr& frame, Stage* stage,
                                const Camera& camera,
                                const ImagePtr& output_image) {
  TRACE_DURATION("gfx", "PaperRenderer2::BeginFrame");
  FXL_DCHECK(!frame_data_);
  frame_data_ = std::make_unique<FrameData>(frame, output_image);

  frame->command_buffer()->TakeWaitSemaphore(
      output_image, vk::PipelineStageFlagBits::eColorAttachmentOutput);
  render_queue_.InitFrame(frame, *stage, camera);
  shape_cache_.BeginFrame(frame_data_->gpu_uploader.get(),
                          frame->frame_number());
}

void PaperRenderer2::EndFrame() {
  TRACE_DURATION("gfx", "PaperRenderer2::EndFrame");
  FXL_DCHECK(frame_data_);
  shape_cache_.EndFrame();
  frame_data_->gpu_uploader->Submit(SemaphorePtr());

  render_queue_.Sort();
  auto& frame = frame_data_->frame;
  BeginRenderPass(frame, frame_data_->output_image);
  render_queue_.GenerateCommands(frame->cmds(), nullptr);
  render_queue_.Clear();
  EndRenderPass(frame);

  frame_data_ = nullptr;
}

void PaperRenderer2::BeginRenderPass(const FramePtr& frame,
                                     const ImagePtr& output_image) {
  TexturePtr depth_texture;
  {
    FXL_DCHECK(!depth_buffers_.empty());
    auto index = frame->frame_number() % depth_buffers_.size();
    depth_texture = depth_buffers_[index];
    if (!depth_texture || depth_texture->width() != output_image->width() ||
        depth_texture->height() != output_image->height()) {
      // Need to generate a new depth buffer.
      TRACE_DURATION("gfx", "PaperRenderer2::DrawFrame (create depth image)");
      depth_texture = escher()->NewAttachmentTexture(
          vk::Format::eD24UnormS8Uint, output_image->width(),
          output_image->height(), output_image->info().sample_count,
          vk::Filter::eLinear);
      depth_buffers_[index] = depth_texture;
    }
  }
  // NOTE: we don't need to keep |depth_texture| alive explicitly because it
  // will be kept alive by the render-pass.

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

void PaperRenderer2::EndRenderPass(const FramePtr& frame) {
  frame->cmds()->EndRenderPass();
  frame->AddTimestamp("finished lighting render pass");
}

void PaperRenderer2::SetNumDepthBuffers(size_t count) {
  FXL_DCHECK(count > 0);
  depth_buffers_.resize(count);
}

}  // namespace escher
