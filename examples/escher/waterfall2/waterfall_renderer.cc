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

WaterfallRendererPtr WaterfallRenderer::New(EscherWeakPtr escher,
                                            ShaderProgramPtr program) {
  return fxl::AdoptRef(
      new WaterfallRenderer(std::move(escher), std::move(program)));
}

WaterfallRenderer::WaterfallRenderer(EscherWeakPtr weak_escher,
                                     ShaderProgramPtr program)
    : Renderer(std::move(weak_escher)), program_(std::move(program)) {
  uniforms_ =
      Buffer::New(escher()->resource_recycler(), escher()->gpu_allocator(),
                  10000, vk::BufferUsageFlagBits::eTransferDst |
                             vk::BufferUsageFlagBits::eTransferSrc |
                             vk::BufferUsageFlagBits::eUniformBuffer,
                  vk::MemoryPropertyFlagBits::eHostVisible |
                      vk::MemoryPropertyFlagBits::eHostCoherent);

  // Need at least one.
  SetNumDepthBuffers(1);
}

WaterfallRenderer::~WaterfallRenderer() { escher()->Cleanup(); }

void WaterfallRenderer::DrawFrame(const FramePtr& frame, const Stage& stage,
                                  const Model& model, const Camera& camera,
                                  const ImagePtr& output_image) {
  TRACE_DURATION("gfx", "WaterfallRenderer::DrawFrame");

  auto cb = frame->cmds();

  // ViewProjection
  mat4& view_projection_matrix = *reinterpret_cast<mat4*>(uniforms_->ptr());
  view_projection_matrix = camera.projection() * camera.transform();
  cb->BindUniformBuffer(0, 0, uniforms_, 0, 16 * sizeof(float));

  // Model transform.
  // TODO(ES-83): Need to set this per-object.
  // As a quick hack, we write into a separate region of the uniform buffer
  // each frame.  That way we can animate a single object without stomping on
  // the matrix being used by the previous frame.
  int64_t offset = 256 + (frame->frame_number() % 3) * 256;
  mat4& model_transform_matrix =
      *reinterpret_cast<glm::mat4*>(uniforms_->ptr() + offset);
  model_transform_matrix = model.objects()[0].transform();
  cb->BindUniformBuffer(1, 0, uniforms_, offset, 16 * sizeof(float));

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

  frame->command_buffer()->TakeWaitSemaphore(
      output_image, vk::PipelineStageFlagBits::eColorAttachmentOutput);

  cb->SetToDefaultState(CommandBuffer::DefaultState::kOpaque);

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
    rp.clear_color[0].setFloat32({0.3f, 0.f, 0.f, 1.f});
  }
  FXL_CHECK(render_pass_info.Validate());

  cb->BeginRenderPass(render_pass_info);

  cb->SetShaderProgram(program_);

  for (auto& o : model.objects()) {
    FXL_DCHECK(o.shape().type() == Shape::Type::kMesh);
    auto& mesh = o.shape().mesh();
    auto& spec = mesh->spec();
    auto vertex_offset = mesh->vertex_buffer_offset();

    frame->command_buffer()->TakeWaitSemaphore(
        mesh, vk::PipelineStageFlagBits::eTopOfPipe);

    cb->BindTexture(1, 1, o.material()->texture());

    cb->BindIndices(mesh->index_buffer(), mesh->index_buffer_offset(),
                    vk::IndexType::eUint32);

    cb->BindVertices(0, mesh->vertex_buffer(), vertex_offset, spec.GetStride());
    cb->SetVertexAttributes(
        0, 0, vk::Format::eR32G32Sfloat,
        spec.GetAttributeOffset(MeshAttribute::kPosition2D));
    cb->SetVertexAttributes(0, 1, vk::Format::eR32G32Sfloat,
                            spec.GetAttributeOffset(MeshAttribute::kUV));

    cb->DrawIndexed(mesh->num_indices());
  }

  cb->EndRenderPass();

  frame->AddTimestamp("finished render pass");
}

void WaterfallRenderer::SetNumDepthBuffers(size_t count) {
  FXL_DCHECK(count > 0);
  depth_buffers_.resize(count);
}
