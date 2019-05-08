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
        << "Swapchain attachment image " << index
        << " must be marked as eStore.";
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
      FXL_LOG(ERROR) << "RenderPass color attachment " << i
                     << " load/clear conflict.";
    });
  }

  // Any attachment marked as clear, load or store must be non-null.
  ForEachBitIndex(clear_attachments | load_attachments | store_attachments,
                  [&](uint32_t i) {
                    if (i >= num_color_attachments) {
                      success = false;
                      FXL_LOG(ERROR) << "Color attachment bit " << i
                                     << " is > num_color_attachments ("
                                     << num_color_attachments << ").";
                    } else if (!color_attachments[i]) {
                      success = false;
                      FXL_LOG(ERROR) << "Color attachment bit " << i
                                     << " is set but attachment is null.";
                    }
                  });

  // |num_color_attachments| must match the number of non-null attachments.
  for (uint32_t i = 0; i < num_color_attachments; ++i) {
    if (!color_attachments[i]) {
      success = false;
      FXL_LOG(ERROR) << "Color attachment " << i << " should not be null.";
    }
  }
  for (uint32_t i = num_color_attachments;
       i < VulkanLimits::kNumColorAttachments; ++i) {
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
    constexpr auto kLoadAndClearDepthStencil =
        kLoadDepthStencilOp | kClearDepthStencilOp;
    // Cannot load and clear the same attachment.
    if (kLoadAndClearDepthStencil == (op_flags & kLoadAndClearDepthStencil)) {
      success = false;
      FXL_LOG(ERROR)
          << "RenderPass depth-stencil attachment load/clear conflict.";
    }

    // Cannot load or store transient image attachments.
    if (depth_stencil_attachment->image()->is_transient()) {
      if (op_flags & kLoadDepthStencilOp) {
        success = false;
        FXL_LOG(ERROR)
            << "Load flag specified for transient depth/stencil attachment.";
      }
      if (op_flags & kStoreDepthStencilOp) {
        success = false;
        FXL_LOG(ERROR)
            << "Load flag specified for transient depth/stencil attachment.";
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
  } else if (op_flags & (kClearDepthStencilOp | kLoadDepthStencilOp |
                         kStoreDepthStencilOp | kOptimalDepthStencilLayoutOp |
                         kDepthStencilReadOnlyLayoutOp)) {
    success = false;
    FXL_LOG(ERROR)
        << "RenderPass has no depth-stencil attachment, but depth-stencil "
           "ops are specified.";
  }

  return success;
}

}  // namespace escher
