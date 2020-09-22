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
  FX_DCHECK(!should_clear_before_use || !should_load_before_use);

  auto load_op = vk::AttachmentLoadOp::eDontCare;
  if (should_clear_before_use) {
    load_op = vk::AttachmentLoadOp::eClear;
  } else if (should_load_before_use) {
    // It doesn't make sense to load a transient attachment; the whole point is
    // to not load/store (and when possible, not even allocate backing memory).
    FX_DCHECK(!color_attachment_infos[index].is_transient);
    // It doesn't make sense to load a swapchain image, since the point is to
    // render a new one every frame.
    // NOTE: we might want to relax this someday, e.g. to support temporal AA.
    // If so, RenderPass::FillColorAttachmentDescription() would need to be
    // adjusted to choose an appropriate initial layout for the attachment.
    FX_DCHECK(!color_attachment_infos[index].is_swapchain_image());

    load_op = vk::AttachmentLoadOp::eLoad;
  }

  auto store_op = vk::AttachmentStoreOp::eDontCare;
  if ((store_attachments & (1u << index)) != 0) {
    // It doesn't make sense to store a transient attachment; the whole point is
    // to not load/store (and when possible, not even allocate backing memory).
    FX_DCHECK(!color_attachment_infos[index].is_transient);
    store_op = vk::AttachmentStoreOp::eStore;
  } else {
    FX_DCHECK(!color_attachment_infos[index].is_swapchain_image())
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
  FX_DCHECK(!should_clear_before_use || !should_load_before_use);

  auto load_op = vk::AttachmentLoadOp::eDontCare;
  if (should_clear_before_use) {
    load_op = vk::AttachmentLoadOp::eClear;
  } else if (should_load_before_use) {
    // It doesn't make sense to load a transient attachment; the whole point is
    // to not load/store (and when possible, not even allocate backing memory).
    FX_DCHECK(!depth_stencil_attachment->image()->is_transient());
    load_op = vk::AttachmentLoadOp::eLoad;
  }

  auto store_op = vk::AttachmentStoreOp::eDontCare;
  if (should_store_after_use) {
    // It doesn't make sense to store a transient attachment; the whole point is
    // to not load/store (and when possible, not even allocate backing memory).
    FX_DCHECK(!depth_stencil_attachment->image()->is_transient());
    store_op = vk::AttachmentStoreOp::eStore;
  }

  return {load_op, store_op};
}

void RenderPassInfo::AttachmentInfo::InitFromImage(const ImagePtr& image) {
  FX_DCHECK(image);
  format = image->format();
  swapchain_layout = image->swapchain_layout();
  sample_count = image->info().sample_count;
  is_transient = image->info().is_transient();
}

void RenderPassInfo::InitRenderPassAttachmentInfosFromImages(RenderPassInfo* rpi) {
  for (size_t i = 0; i < rpi->num_color_attachments; ++i) {
    FX_DCHECK(rpi->color_attachments[i]);
    rpi->color_attachment_infos[i].InitFromImage(rpi->color_attachments[i]->image());
  }

  for (size_t i = rpi->num_color_attachments; i < VulkanLimits::kNumColorAttachments; ++i) {
    FX_DCHECK(!rpi->color_attachments[i]);
    rpi->color_attachment_infos[i] = RenderPassInfo::AttachmentInfo{};
  }

  if (rpi->depth_stencil_attachment) {
    rpi->depth_stencil_attachment_info.InitFromImage(rpi->depth_stencil_attachment->image());
  } else {
    rpi->depth_stencil_attachment_info = RenderPassInfo::AttachmentInfo{};
  }
}

// TODO(fxbug.dev/7174): unit-tests for validation.
// TODO(fxbug.dev/7174): what other validation should be performed?
bool RenderPassInfo::Validate() const {
  bool success = true;

  // We can't rely on whether |depth_stencil_attachment| is null or not, because we want to be
  // able to create render passes without providing any images. Instead, we treat
  // |depth_stencil_attachment_info| as the source of truth.
  const bool has_depth_stencil_attachment =
      depth_stencil_attachment_info.format != vk::Format::eUndefined;

  // There must be at least one attachment.
  if (num_color_attachments == 0 && !has_depth_stencil_attachment) {
    success = false;
    FX_LOGS(ERROR) << "RenderPass has no attachments.";
  }

  // The attachment infos must match the info in the corresponding image, if any.
  for (uint32_t i = 0; i < num_color_attachments; ++i) {
    if (color_attachments[i]) {
      auto& image = color_attachments[i]->image();
      if (color_attachment_infos[i].format != image->format()) {
        success = false;
        FX_LOGS(ERROR) << "Color attachment info " << i << " format mismatch.";
      }
      if (color_attachment_infos[i].swapchain_layout != image->swapchain_layout()) {
        success = false;
        FX_LOGS(ERROR) << "Color attachment info " << i << " swapchain_layout mismatch.";
      }
      if (color_attachment_infos[i].sample_count != image->info().sample_count) {
        success = false;
        FX_LOGS(ERROR) << "Color attachment info " << i << " sample_count mismatch.";
      }
      if (color_attachment_infos[i].is_transient != image->is_transient()) {
        success = false;
        FX_LOGS(ERROR) << "Color attachment info " << i << " is_transient mismatch.";
      }
    }
  }
  if (depth_stencil_attachment) {
    auto& image = depth_stencil_attachment->image();

    if (depth_stencil_attachment_info.format != image->format()) {
      success = false;
      FX_LOGS(ERROR) << "Depth attachment info format mismatch.";
    }
    if (depth_stencil_attachment_info.swapchain_layout != image->swapchain_layout()) {
      success = false;
      FX_LOGS(ERROR) << "Depth attachment info swapchain_layout mismatch.";
    }
    if (depth_stencil_attachment_info.sample_count != image->info().sample_count) {
      success = false;
      FX_LOGS(ERROR) << "Depth attachment info sample_count mismatch.";
    }
    if (depth_stencil_attachment_info.is_transient != image->is_transient()) {
      success = false;
      FX_LOGS(ERROR) << "Depth attachment info is_transient mismatch.";
    }
  }

  // Cannot load and clear the same attachment.
  if (auto load_clear_conflicts = clear_attachments & load_attachments) {
    success = false;
    ForEachBitIndex(load_clear_conflicts, [](uint32_t i) {
      FX_LOGS(ERROR) << "RenderPass color attachment " << i << " load/clear conflict.";
    });
  }

  // Any attachment marked as clear, load or store must be non-null.
  ForEachBitIndex(clear_attachments | load_attachments | store_attachments, [&](uint32_t i) {
    if (i >= num_color_attachments) {
      success = false;
      FX_LOGS(ERROR) << "Color attachment bit " << i << " is > num_color_attachments ("
                     << num_color_attachments << ").";
    }
  });

  // All of the attachments up to |num_color_attachments| must have a defined format, and
  // none of the subsequent attachments should have a defined format.
  for (uint32_t i = 0; i < num_color_attachments; ++i) {
    if (vk::Format::eUndefined == color_attachment_infos[i].format) {
      success = false;
      FX_LOGS(ERROR) << "Color attachment " << i << " should have a defined format.";
    }
  }
  for (uint32_t i = num_color_attachments; i < VulkanLimits::kNumColorAttachments; ++i) {
    if (vk::Format::eUndefined != color_attachment_infos[i].format) {
      success = false;
      FX_LOGS(ERROR) << "Color attachment " << i << " should not have a defined format.";
    }
  }

  if (has_depth_stencil_attachment) {
    constexpr auto kLoadAndClearDepthStencil = kLoadDepthStencilOp | kClearDepthStencilOp;
    // Cannot load and clear the same attachment.
    if (kLoadAndClearDepthStencil == (op_flags & kLoadAndClearDepthStencil)) {
      success = false;
      FX_LOGS(ERROR) << "RenderPass depth-stencil attachment load/clear conflict.";
    }

    // Cannot load or store transient image attachments.
    if (depth_stencil_attachment_info.is_transient) {
      if (op_flags & kLoadDepthStencilOp) {
        success = false;
        FX_LOGS(ERROR) << "Load flag specified for transient depth/stencil attachment.";
      }
      if (op_flags & kStoreDepthStencilOp) {
        success = false;
        FX_LOGS(ERROR) << "Load flag specified for transient depth/stencil attachment.";
      }
    }

    // Cannot specify two conflicting depth-stencil layouts.
    constexpr auto kBothDepthStencilLayouts =
        kOptimalDepthStencilLayoutOp | kDepthStencilReadOnlyLayoutOp;
    if (kBothDepthStencilLayouts == (op_flags & kBothDepthStencilLayouts)) {
      success = false;
      FX_LOGS(ERROR) << "Depth attachment is specified as both optimal "
                        "read-only and read-write.";
    }
  } else if (op_flags & (kClearDepthStencilOp | kLoadDepthStencilOp | kStoreDepthStencilOp |
                         kOptimalDepthStencilLayoutOp | kDepthStencilReadOnlyLayoutOp)) {
    success = false;
    FX_LOGS(ERROR) << "RenderPass has no depth-stencil attachment, but depth-stencil "
                      "ops are specified.";
  }

  return success;
}

// Used by InitRenderPassInfo() and InitRenderPassInfoHelper().
static constexpr uint32_t kRenderTargetAttachmentIndex = 0;
static constexpr uint32_t kResolveTargetAttachmentIndex = 1;

// Helper function which factors out common code from the two InitRenderPassInfo() variants.
static void InitRenderPassInfoHelper(RenderPassInfo* rp,
                                     const RenderPassInfo::AttachmentInfo& color_info,
                                     const RenderPassInfo::AttachmentInfo& depth_stencil_info,
                                     const RenderPassInfo::AttachmentInfo* msaa_info) {
  FX_DCHECK(color_info.sample_count == 1);
  FX_DCHECK((!msaa_info && depth_stencil_info.sample_count == 1) ||
            (msaa_info && msaa_info->sample_count > 1 &&
             msaa_info->sample_count == depth_stencil_info.sample_count));

  rp->color_attachment_infos[0] = color_info;
  rp->depth_stencil_attachment_info = depth_stencil_info;

  {
    rp->num_color_attachments = 1;
    // Clear and store color attachment 0, the sole color attachment.
    rp->clear_attachments = 1u << kRenderTargetAttachmentIndex;
    rp->store_attachments = 1u << kRenderTargetAttachmentIndex;

    // Standard flags for a depth-testing render-pass that needs to first clear
    // the depth image.
    rp->op_flags = RenderPassInfo::kClearDepthStencilOp | RenderPassInfo::kOptimalColorLayoutOp |
                   RenderPassInfo::kOptimalDepthStencilLayoutOp;
    // NOTE: the flags above assume that there is a depth/stencil attachment.  If not, the flags
    // will need to be modified (shouldn't be hard, but out of scope of current CL).
    FX_DCHECK(depth_stencil_info.format != vk::Format::eUndefined);

    rp->clear_color[0].setFloat32({0.f, 0.f, 0.f, 0.f});

    // If MSAA is enabled, we need to explicitly specify the sub-pass in order
    // to specify the resolve attachment.  Otherwise we allow a default sub-pass
    // to be created.
    if (msaa_info) {
      FX_DCHECK(depth_stencil_info.sample_count == msaa_info->sample_count);
      FX_DCHECK(rp->num_color_attachments == 1 && rp->clear_attachments == 1u);
      // Move the output image to attachment #1, so that attachment #0 is always
      // the attachment that we render into.
      rp->color_attachment_infos[kResolveTargetAttachmentIndex] = color_info;
      rp->color_attachment_infos[kRenderTargetAttachmentIndex] = *msaa_info;

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

  for (size_t i = rp->num_color_attachments; i < VulkanLimits::kNumColorAttachments; ++i) {
    rp->color_attachment_infos[i] = {};
    rp->color_attachments[i] = nullptr;
  }
}

bool RenderPassInfo::InitRenderPassInfo(RenderPassInfo* rp, vk::Rect2D render_area,
                                        const ImagePtr& output_image,
                                        const TexturePtr& depth_texture,
                                        const TexturePtr& msaa_texture,
                                        ImageViewAllocator* allocator) {
  FX_DCHECK(output_image->info().sample_count == 1);
  rp->render_area = render_area;

  AttachmentInfo color_info;
  AttachmentInfo depth_stencil_info;
  AttachmentInfo msaa_info;
  if (output_image) {
    if (!output_image->is_swapchain_image()) {
      FX_LOGS(ERROR) << "RenderPassInfo::InitRenderPassInfo(): Output image doesn't have valid "
                        "swapchain layout.";
      return false;
    }
    if (output_image->swapchain_layout() != output_image->layout()) {
      FX_LOGS(ERROR) << "RenderPassInfo::InitRenderPassInfo(): Current layout of output image "
                        "does not match its swapchain layout.";
      return false;
    }
    color_info.InitFromImage(output_image);
  }
  if (depth_texture) {
    depth_stencil_info.InitFromImage(depth_texture->image());
  }
  if (msaa_texture) {
    msaa_info.InitFromImage(msaa_texture->image());
  }

  InitRenderPassInfoHelper(rp, color_info, depth_stencil_info, msaa_texture ? &msaa_info : nullptr);

  // TODO(fxbug.dev/43279): Can we get away sharing image views across multiple RenderPassInfo
  // structs?
  ImageViewPtr output_image_view =
      allocator ? allocator->ObtainImageView(output_image) : ImageView::New(output_image);

  // If MSAA is enabled then we render into |msaa_texture| instead of directly into |output_image|.
  // Therefore we need to adjust the attachment images.
  if (msaa_texture) {
    rp->color_attachments[kRenderTargetAttachmentIndex] = msaa_texture;
    rp->color_attachments[kResolveTargetAttachmentIndex] = std::move(output_image_view);
  } else {
    rp->color_attachments[kRenderTargetAttachmentIndex] = std::move(output_image_view);
  }
  rp->depth_stencil_attachment = depth_texture;
  return true;
}

bool RenderPassInfo::InitRenderPassInfo(RenderPassInfo* rp,
                                        const RenderPassInfo::AttachmentInfo& color_info,
                                        vk::Format depth_stencil_format, vk::Format msaa_format,
                                        uint32_t sample_count, bool use_transient_depth_and_msaa) {
  const bool has_msaa = sample_count != 1;
  FX_DCHECK(!has_msaa || (msaa_format != vk::Format::eUndefined));

  RenderPassInfo::AttachmentInfo depth_stencil_info;
  depth_stencil_info.format = depth_stencil_format;
  depth_stencil_info.sample_count = sample_count;

  RenderPassInfo::AttachmentInfo msaa_info;
  msaa_info.format = msaa_format;
  msaa_info.sample_count = sample_count;

  InitRenderPassInfoHelper(rp, color_info, depth_stencil_info, has_msaa ? &msaa_info : nullptr);
  return true;
}

}  // namespace escher
