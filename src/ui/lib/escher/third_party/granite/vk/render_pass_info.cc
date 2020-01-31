/* Copyright (c) 2017 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

// Based on the following files from the Granite rendering engine:
// - vulkan/render_pass.cpp

#include "src/ui/lib/escher/third_party/granite/vk/render_pass_info.h"

#include "src/ui/lib/escher/util/bit_ops.h"
#include "src/ui/lib/escher/vk/texture.h"

namespace escher {

std::pair<vk::AttachmentLoadOp, vk::AttachmentStoreOp>
RenderPassInfo::LoadStoreOpsForColorAttachment(uint32_t index) const {
  const bool should_clear_before_use = (clear_attachments & (1u << index)) != 0;
  const bool should_load_before_use = (load_attachments & (1u << index)) != 0;
  FXL_DCHECK(!should_clear_before_use || !should_load_before_use);

  auto load_op = vk::AttachmentLoadOp::eDontCare;
  if (should_clear_before_use) {
    load_op = vk::AttachmentLoadOp::eClear;
  } else if (should_load_before_use) {
    // It doesn't make sense to load a transient attachment; the whole point is
    // to not load/store (and when possible, not even allocate backing memory).
    FXL_DCHECK(!color_attachments[index]->image()->is_transient());
    // It doesn't make sense to load a swapchain image, since the point is to
    // render a new one every frame.
    // NOTE: we might want to relax this someday, e.g. to support temporal AA.
    // If so, RenderPass::FillColorAttachmentDescription() would need to be
    // adjusted to choose an appropriate initial layout for the attachment.
    FXL_DCHECK(!color_attachments[index]->image()->is_swapchain_image());

    load_op = vk::AttachmentLoadOp::eLoad;
  }

  auto store_op = vk::AttachmentStoreOp::eDontCare;
  if ((store_attachments & (1u << index)) != 0) {
    // It doesn't make sense to store a transient attachment; the whole point is
    // to not load/store (and when possible, not even allocate backing memory).
    FXL_DCHECK(!color_attachments[index]->image()->is_transient());
    store_op = vk::AttachmentStoreOp::eStore;
  } else {
    FXL_DCHECK(!color_attachments[index]->image()->is_swapchain_image())
        << "Swapchain attachment image " << index << " must be marked as eStore.";
  }

  return {load_op, store_op};
}

std::pair<vk::AttachmentLoadOp, vk::AttachmentStoreOp>
RenderPassInfo::LoadStoreOpsForDepthStencilAttachment() const {
  const bool should_clear_before_use =
      static_cast<bool>(op_flags & RenderPassInfo::kClearDepthStencilOp);
  const bool should_load_before_use =
      static_cast<bool>(op_flags & RenderPassInfo::kLoadDepthStencilOp);
  const bool should_store_after_use =
      static_cast<bool>(op_flags & RenderPassInfo::kStoreDepthStencilOp);
  FXL_DCHECK(!should_clear_before_use || !should_load_before_use);

  auto load_op = vk::AttachmentLoadOp::eDontCare;
  if (should_clear_before_use) {
    load_op = vk::AttachmentLoadOp::eClear;
  } else if (should_load_before_use) {
    // It doesn't make sense to load a transient attachment; the whole point is
    // to not load/store (and when possible, not even allocate backing memory).
    FXL_DCHECK(!depth_stencil_attachment->image()->is_transient());
    load_op = vk::AttachmentLoadOp::eLoad;
  }

  auto store_op = vk::AttachmentStoreOp::eDontCare;
  if (should_store_after_use) {
    // It doesn't make sense to store a transient attachment; the whole point is
    // to not load/store (and when possible, not even allocate backing memory).
    FXL_DCHECK(!depth_stencil_attachment->image()->is_transient());
    store_op = vk::AttachmentStoreOp::eStore;
  }

  return {load_op, store_op};
}

// TODO(ES-83): unit-tests for validation.
// TODO(ES-83): what other validation should be performed?
bool RenderPassInfo::Validate() const {
  bool success = true;

  // Cannot load and clear the same attachment.
  if (auto load_clear_conflicts = clear_attachments & load_attachments) {
    success = false;
    ForEachBitIndex(load_clear_conflicts, [](uint32_t i) {
      FXL_LOG(ERROR) << "RenderPass color attachment " << i << " load/clear conflict.";
    });
  }

  // Any attachment marked as clear, load or store must be non-null.
  ForEachBitIndex(clear_attachments | load_attachments | store_attachments, [&](uint32_t i) {
    if (i >= num_color_attachments) {
      success = false;
      FXL_LOG(ERROR) << "Color attachment bit " << i << " is > num_color_attachments ("
                     << num_color_attachments << ").";
    } else if (!color_attachments[i]) {
      success = false;
      FXL_LOG(ERROR) << "Color attachment bit " << i << " is set but attachment is null.";
    }
  });

  // |num_color_attachments| must match the number of non-null attachments.
  for (uint32_t i = 0; i < num_color_attachments; ++i) {
    if (!color_attachments[i]) {
      success = false;
      FXL_LOG(ERROR) << "Color attachment " << i << " should not be null.";
    }
  }
  for (uint32_t i = num_color_attachments; i < VulkanLimits::kNumColorAttachments; ++i) {
    if (color_attachments[i]) {
      success = false;
      FXL_LOG(ERROR) << "Color attachment " << i << " should be null.";
    }
  }

  // There must be at least one attachment.
  if (!num_color_attachments && !depth_stencil_attachment) {
    success = false;
    FXL_LOG(ERROR) << "RenderPass has no attachments.";
  }

  if (depth_stencil_attachment) {
    constexpr auto kLoadAndClearDepthStencil = kLoadDepthStencilOp | kClearDepthStencilOp;
    // Cannot load and clear the same attachment.
    if (kLoadAndClearDepthStencil == (op_flags & kLoadAndClearDepthStencil)) {
      success = false;
      FXL_LOG(ERROR) << "RenderPass depth-stencil attachment load/clear conflict.";
    }

    // Cannot load or store transient image attachments.
    if (depth_stencil_attachment->image()->is_transient()) {
      if (op_flags & kLoadDepthStencilOp) {
        success = false;
        FXL_LOG(ERROR) << "Load flag specified for transient depth/stencil attachment.";
      }
      if (op_flags & kStoreDepthStencilOp) {
        success = false;
        FXL_LOG(ERROR) << "Load flag specified for transient depth/stencil attachment.";
      }
    }

    // Cannot specify two conflicting depth-stencil layouts.
    constexpr auto kBothDepthStencilLayouts =
        kOptimalDepthStencilLayoutOp | kDepthStencilReadOnlyLayoutOp;
    if (kBothDepthStencilLayouts == (op_flags & kBothDepthStencilLayouts)) {
      success = false;
      FXL_LOG(ERROR) << "Depth attachment is specified as both optimal "
                        "read-only and read-write.";
    }
  } else if (op_flags & (kClearDepthStencilOp | kLoadDepthStencilOp | kStoreDepthStencilOp |
                         kOptimalDepthStencilLayoutOp | kDepthStencilReadOnlyLayoutOp)) {
    success = false;
    FXL_LOG(ERROR) << "RenderPass has no depth-stencil attachment, but depth-stencil "
                      "ops are specified.";
  }

  return success;
}

void RenderPassInfo::InitRenderPassInfo(RenderPassInfo* rp, vk::Rect2D render_area,
                                        const ImagePtr& output_image,
                                        const TexturePtr& depth_texture,
                                        const TexturePtr& msaa_texture,
                                        ImageViewAllocator* allocator) {
  FXL_DCHECK(output_image->info().sample_count == 1);
  rp->render_area = render_area;

  static constexpr uint32_t kRenderTargetAttachmentIndex = 0;
  static constexpr uint32_t kResolveTargetAttachmentIndex = 1;
  {
    // TODO(43279): Can we get away sharing image views across multiple RenderPassInfo structs?
    if (allocator) {
      rp->color_attachments[kRenderTargetAttachmentIndex] =
          allocator->ObtainImageView(output_image);
    } else {
      rp->color_attachments[kRenderTargetAttachmentIndex] = ImageView::New(output_image);
    }
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
      FXL_DCHECK(depth_texture->sample_count() == msaa_texture->sample_count());
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
