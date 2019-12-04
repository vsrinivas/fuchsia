// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/renderer/renderer.h"

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/impl/command_buffer_pool.h"
#include "src/ui/lib/escher/impl/image_cache.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/profiling/timestamp_profiler.h"
#include "src/ui/lib/escher/scene/stage.h"
#include "src/ui/lib/escher/util/stopwatch.h"
#include "src/ui/lib/escher/util/trace_macros.h"
#include "src/ui/lib/escher/vk/framebuffer.h"
#include "src/ui/lib/escher/vk/image.h"
#include "src/ui/lib/escher/vk/texture.h"

namespace escher {

Renderer::Renderer(EscherWeakPtr weak_escher)
    : context_(weak_escher->vulkan_context()), escher_(std::move(weak_escher)) {
  escher()->IncrementRendererCount();
}

Renderer::~Renderer() { escher()->DecrementRendererCount(); }

Renderer::FrameData::FrameData(const FramePtr& frame_in,
                               std::shared_ptr<BatchGpuUploader> gpu_uploader_in,
                               const ImagePtr& output_image_in,
                               std::pair<TexturePtr, TexturePtr> depth_and_msaa_textures)
    : frame(frame_in),
      output_image(output_image_in),
      depth_texture(std::move(depth_and_msaa_textures.first)),
      msaa_texture(std::move(depth_and_msaa_textures.second)),
      gpu_uploader(gpu_uploader_in) {}

Renderer::FrameData::~FrameData() = default;

std::pair<TexturePtr, TexturePtr> Renderer::ObtainDepthAndMsaaTextures(
    const FramePtr& frame, const ImageInfo& info, uint32_t msaa_sample_count,
    vk::Format depth_stencil_format) {
  FXL_DCHECK(!depth_buffers_.empty());

  // Support for other sample_counts should fairly easy to add, if necessary.
  FXL_DCHECK(info.sample_count == 1);

  auto index = frame->frame_number() % depth_buffers_.size();
  TexturePtr& depth_texture = depth_buffers_[index];
  TexturePtr& msaa_texture = msaa_buffers_[index];

  const bool realloc_textures =
      !depth_texture ||
      (depth_texture->image()->use_protected_memory() != frame->use_protected_memory()) ||
      info.width != depth_texture->width() || info.height != depth_texture->height() ||
      msaa_sample_count != depth_texture->image()->info().sample_count;

  if (realloc_textures) {
    // Need to generate a new depth buffer.
    {
      TRACE_DURATION("gfx", "PaperRenderer::ObtainDepthAndMsaaTextures (new depth)");
      depth_texture = escher()->NewAttachmentTexture(
          depth_stencil_format, info.width, info.height, msaa_sample_count, vk::Filter::eLinear,
          vk::ImageUsageFlags(), /*is_transient_attachment=*/false,
          /*is_input_attachment=*/false, /*use_unnormalized_coordinates=*/false,
          frame->use_protected_memory() ? vk::MemoryPropertyFlagBits::eProtected
                                        : vk::MemoryPropertyFlags());
    }
    // If the sample count is 1, there is no need for a MSAA buffer.
    if (msaa_sample_count == 1) {
      msaa_texture = nullptr;
    } else {
      TRACE_DURATION("gfx", "Renderer::ObtainDepthAndMsaaTextures (new msaa)");
      // TODO(SCN-634): use lazy memory allocation and transient attachments
      // when available.
      msaa_texture = escher()->NewAttachmentTexture(
          info.format, info.width, info.height, msaa_sample_count, vk::Filter::eLinear,
          vk::ImageUsageFlags(), /*is_transient_attachment=*/false,
          /*is_input_attachment=*/false, /*use_unnormalized_coordinates=*/false,
          frame->use_protected_memory() ? vk::MemoryPropertyFlagBits::eProtected
                                        : vk::MemoryPropertyFlags()
          // TODO(ES-73): , vk::ImageUsageFlagBits::eTransientAttachment
      );

      frame->cmds()->ImageBarrier(msaa_texture->image(), vk::ImageLayout::eUndefined,
                                  vk::ImageLayout::eColorAttachmentOptimal,
                                  vk::PipelineStageFlagBits::eAllGraphics, vk::AccessFlags(),
                                  vk::PipelineStageFlagBits::eColorAttachmentOutput,
                                  vk::AccessFlagBits::eColorAttachmentWrite);
    }
  }
  return {depth_texture, msaa_texture};
}

void Renderer::InitRenderPassInfo(RenderPassInfo* rp, ImageViewAllocator* allocator,
                                  const FrameData& frame_data, vk::Rect2D render_area) {
  const ImagePtr& output_image = frame_data.output_image;
  const TexturePtr& depth_texture = frame_data.depth_texture;
  const TexturePtr& msaa_texture = frame_data.msaa_texture;
  rp->render_area = render_area;

  static constexpr uint32_t kRenderTargetAttachmentIndex = 0;
  static constexpr uint32_t kResolveTargetAttachmentIndex = 1;
  {
    rp->color_attachments[kRenderTargetAttachmentIndex] = allocator->ObtainImageView(output_image);
    rp->num_color_attachments = 1;
    // Clear and store color attachment 0, the sole color attachment.
    rp->clear_attachments = 1u << kRenderTargetAttachmentIndex;
    rp->store_attachments = 1u << kRenderTargetAttachmentIndex;
    // NOTE: we don't need to keep |depth_texture| alive explicitly because it
    // will be kept alive by the render-pass.
    rp->depth_stencil_attachment = depth_texture;
    // Standard flags for a depth-testing render-pass that needs to first clear
    // the depth image.
    rp->op_flags = RenderPassInfo::kClearDepthStencilOp | RenderPassInfo::kOptimalColorLayoutOp |
                   RenderPassInfo::kOptimalDepthStencilLayoutOp;
    rp->clear_color[0].setFloat32({0.f, 0.f, 0.f, 0.f});

    // If MSAA is enabled, we need to explicitly specify the sub-pass in order
    // to specify the resolve attachment.  Otherwise we allow a default subclass
    // to be created.
    if (msaa_texture) {
      FXL_DCHECK(rp->num_color_attachments == 1 && rp->clear_attachments == 1u);
      // Move the output image to attachment #1, so that attachment #0 is always
      // the attachment that we render into.
      rp->color_attachments[kResolveTargetAttachmentIndex] =
          std::move(rp->color_attachments[kRenderTargetAttachmentIndex]);
      rp->color_attachments[kRenderTargetAttachmentIndex] = msaa_texture;
      rp->num_color_attachments = 2;

      // Now that the output image is attachment #1, that's the one we need to
      // store.
      rp->store_attachments = 1u << kResolveTargetAttachmentIndex;

      rp->subpasses.push_back(RenderPassInfo::Subpass{
          .color_attachments = {kRenderTargetAttachmentIndex},
          .input_attachments = {},
          .resolve_attachments = {kResolveTargetAttachmentIndex},
          .num_color_attachments = 1,
          .num_input_attachments = 0,
          .num_resolve_attachments = 1,
      });
    }
  }
  FXL_DCHECK(rp->Validate());
}

}  // namespace escher
