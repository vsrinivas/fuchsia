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

#include "src/ui/lib/escher/third_party/granite/vk/render_pass.h"

#include <lib/syslog/cpp/macros.h>

#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/resources/resource_recycler.h"
#include "src/ui/lib/escher/util/bit_ops.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/util/stack_allocator.h"
#include "src/ui/lib/escher/vk/render_pass_info.h"

namespace escher {
namespace impl {

const ResourceTypeInfo impl::RenderPass::kTypeInfo("impl::RenderPass", ResourceType::kResource,
                                                   ResourceType::kImplRenderPass);

namespace {
// Helper function for constructor.  If |info| has explicit subpasses, return a
// pointer to the first one.  Otherwise, populate |default_subpass| and return
// the pointer to it.
const RenderPassInfo::Subpass* GetPointerToFirstSubpass(const RenderPassInfo& info,
                                                        RenderPassInfo::Subpass* default_subpass) {
  if (info.subpasses.empty()) {
    // Populate default subpass.
    default_subpass->num_color_attachments = info.num_color_attachments;
    default_subpass->depth_stencil_mode = RenderPassInfo::DepthStencil::kReadWrite;
    for (uint32_t i = 0; i < info.num_color_attachments; i++) {
      default_subpass->color_attachments[i] = i;
    }
    return default_subpass;
  }
  return info.subpasses.data();
}

// Helper function for constructor.  Return true if the attachment requires
// an implicit layout transition (because it is a swapchain image or transient
// attachment), and false otherwise.
bool FillColorAttachmentDescription(const RenderPassInfo& rpi,
                                    vk::AttachmentDescription* attachment_descriptions,
                                    uint32_t index) {
#ifndef NDEBUG
  auto pair = image_utils::IsDepthStencilFormat(rpi.color_attachment_infos[index].format);
  FX_DCHECK(!pair.first && !pair.second) << "Color attachment cannot use depth/stencil format.";
#endif

  auto& desc = attachment_descriptions[index];
  const auto& color_info = rpi.color_attachment_infos[index];
  const bool is_swapchain_image = color_info.is_swapchain_image();

  // TODO(fxbug.dev/7166): support for transient images.  What's missing?
  FX_DCHECK(!color_info.is_transient || !is_swapchain_image)
      << "transient+swapchain images not yet handled.";

  const auto load_store_ops_pair = rpi.LoadStoreOpsForColorAttachment(index);
  const vk::AttachmentLoadOp load_op = load_store_ops_pair.first;
  const vk::AttachmentStoreOp store_op = load_store_ops_pair.second;

  desc.flags = vk::AttachmentDescriptionFlags();
  desc.format = color_info.format;
  desc.samples = SampleCountFlagBitsFromInt(color_info.sample_count);
  desc.loadOp = load_op;
  desc.storeOp = store_op;
  // Stencil ops are inapplicable (this is a color attachment), hence eDontCare.
  desc.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
  desc.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;

  if (color_info.is_transient) {
    FX_DCHECK(load_op == vk::AttachmentLoadOp::eDontCare);
    desc.initialLayout = vk::ImageLayout::eUndefined;
    // This will be filled in later with the layout of the last subpass that
    // uses this attachment, in order to avoid an unnecessary transition at
    // the end of the render pass.
    desc.finalLayout = vk::ImageLayout::eUndefined;
    return true;
  } else if (is_swapchain_image) {
    desc.initialLayout = color_info.swapchain_layout;
    desc.finalLayout = color_info.swapchain_layout;
    return true;
  } else if (rpi.op_flags & RenderPassInfo::kOptimalColorLayoutOp) {
    desc.initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
    // This will be filled in later with the layout of the last subpass that
    // uses this attachment, in order to avoid an unnecessary transition at
    // the end of the render pass.
    desc.finalLayout = vk::ImageLayout::eUndefined;
    return false;
  } else {
    desc.initialLayout = vk::ImageLayout::eGeneral;
    desc.finalLayout = vk::ImageLayout::eGeneral;
    return false;
  }
}

// Helper function for constructor.  Return true if the attachment requires
// an implicit layout transition (because it is a swapchain image or transient
// attachment), and false otherwise.
bool FillDepthStencilAttachmentDescription(const RenderPassInfo& rpi,
                                           vk::AttachmentDescription* desc) {
  desc->flags = vk::AttachmentDescriptionFlags();
  desc->format = rpi.depth_stencil_attachment_info.format;
  desc->samples = SampleCountFlagBitsFromInt(rpi.depth_stencil_attachment_info.sample_count);
  desc->loadOp = vk::AttachmentLoadOp::eDontCare;
  desc->storeOp = vk::AttachmentStoreOp::eDontCare;

  const auto load_store_ops_pair = rpi.LoadStoreOpsForDepthStencilAttachment();
  const vk::AttachmentLoadOp load_op = load_store_ops_pair.first;
  const vk::AttachmentStoreOp store_op = load_store_ops_pair.second;

  const auto bool_pair = image_utils::IsDepthStencilFormat(desc->format);
  if (bool_pair.first) {
    // Attachment has a depth component.
    desc->loadOp = load_op;
    desc->storeOp = store_op;
  }
  if (bool_pair.second) {
    // Attachment has a stencil component.
    desc->stencilLoadOp = load_op;
    desc->stencilStoreOp = store_op;
  }

  if (rpi.depth_stencil_attachment_info.is_transient) {
    FX_DCHECK(load_op != vk::AttachmentLoadOp::eLoad);

    desc->initialLayout = vk::ImageLayout::eUndefined;

    // This will be filled in later with the layout of the last subpass that
    // uses this attachment, in order to avoid an unnecessary transition at
    // the end of the render pass.
    desc->finalLayout = vk::ImageLayout::eUndefined;

    return true;
  }

  vk::ImageLayout layout = vk::ImageLayout::eGeneral;
  if (rpi.op_flags & RenderPassInfo::kOptimalDepthStencilLayoutOp) {
    layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
  } else if (rpi.op_flags & RenderPassInfo::kDepthStencilReadOnlyLayoutOp) {
    // NOTE: this flag and the one above are mutually exclusive.  This is
    // enforced by RenderPassInfo::Validate().
    layout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
  }

  // Since the attachment is not being loaded, we make the render pass more
  // more flexible regarding the attachments it will accept by setting the
  // layout to eUndefined.  This should incur no additional
  // performance cost.
  desc->initialLayout =
      desc->loadOp == vk::AttachmentLoadOp::eLoad ? layout : vk::ImageLayout::eUndefined;

  // TODO(fxbug.dev/7174): If the attachment is not being stored, then the most
  // performant choice is to leave it in the same layout as the last subpass
  // that uses this attachment, in order to avoid an extra transition at the end
  // of the render pass (which would be indicated by setting |finalLayout| to
  // |eDontCare|, so that it would later automatically be set to
  // |current_layout|).
  desc->finalLayout = layout;

  return false;
}

// Find and return the vk::AttachmentReference that references the specified
// attachment in the specified subpass, or nullptr if the attachment is not
// referenced as a color attachment in that subpass.
vk::AttachmentReference* FindColorAttachmentRef(vk::SubpassDescription* subpass,
                                                uint32_t attachment_index) {
  auto* colors = subpass->pColorAttachments;
  const uint32_t count = subpass->colorAttachmentCount;
  for (uint32_t i = 0; i < count; i++) {
    if (colors[i].attachment == attachment_index) {
      return const_cast<vk::AttachmentReference*>(&colors[i]);
    }
  }
  return nullptr;
};

// Find and return the vk::AttachmentReference that references the specified
// attachment in the specified subpass, or nullptr if the attachment is not
// referenced as a resolve attachment in that subpass.
vk::AttachmentReference* FindResolveAttachmentRef(vk::SubpassDescription* subpass,
                                                  uint32_t attachment_index) {
  if (auto* resolves = subpass->pResolveAttachments) {
    const uint32_t count = subpass->colorAttachmentCount;
    for (uint32_t i = 0; i < count; i++) {
      if (resolves[i].attachment == attachment_index) {
        return const_cast<vk::AttachmentReference*>(&resolves[i]);
      }
    }
  }
  return nullptr;
};

// Find and return the vk::AttachmentReference that references the specified
// attachment in the specified subpass, or nullptr if the attachment is not
// referenced as an input attachment in that subpass.
vk::AttachmentReference* FindInputAttachmentRef(vk::SubpassDescription* subpass,
                                                uint32_t attachment_index) {
  auto* inputs = subpass->pInputAttachments;
  const uint32_t count = subpass->inputAttachmentCount;
  for (uint32_t i = 0; i < count; i++) {
    if (inputs[i].attachment == attachment_index) {
      return const_cast<vk::AttachmentReference*>(&inputs[i]);
    }
  }
  return nullptr;
}

// Find and return the vk::AttachmentReference that references the specified
// attachment in the specified subpass, or nullptr if the attachment is not
// the depth-stencil attachment for that subpass.
vk::AttachmentReference* FindDepthStencilAttachmentRef(vk::SubpassDescription* subpass,
                                                       uint32_t attachment_index) {
  if (subpass->pDepthStencilAttachment->attachment == attachment_index) {
    return const_cast<vk::AttachmentReference*>(subpass->pDepthStencilAttachment);
  }
  return nullptr;
};

}  // namespace

RenderPass::RenderPass(ResourceRecycler* recycler, const RenderPassInfo& info)
    : Resource(recycler) {
  FX_DCHECK(info.Validate());
  num_color_attachments_ = info.num_color_attachments;

  // If the RenderPassInfo doesn't have any subpasses, set up a single default
  // subpass, otherwise use the provided subpasses. Either way, subsequent code
  // will use |info_subpasses| and |num_info_subpasses| without considering
  // where they came from.
  RenderPassInfo::Subpass default_subpass;
  const RenderPassInfo::Subpass* info_subpasses = GetPointerToFirstSubpass(info, &default_subpass);
  const uint32_t num_info_subpasses =
      info.subpasses.empty() ? 1u : static_cast<uint32_t>(info.subpasses.size());
  FX_DCHECK(num_info_subpasses <= 32);

  vk::AttachmentDescription attachments[VulkanLimits::kNumColorAttachments + 1];
  const bool has_depth_stencil_attachment =
      info.depth_stencil_attachment_info.format != vk::Format::eUndefined;
  const uint32_t num_attachments =
      info.num_color_attachments + (has_depth_stencil_attachment ? 1 : 0);
  uint32_t implicit_transitions = 0;

  // Initialize description of each color attachment (load/store ops, format,
  // and layout).
  for (uint32_t i = 0; i < info.num_color_attachments; i++) {
    if (FillColorAttachmentDescription(info, attachments, i)) {
      implicit_transitions |= 1u << i;
    }
    color_formats_[i] = attachments[i].format;
  }

  // Initialize description of depth-stencil attachment (load/store ops, format,
  // and layout).
  depth_stencil_format_ = vk::Format::eUndefined;
  if (has_depth_stencil_attachment) {
    auto attachment_description = attachments + info.num_color_attachments;
    if (FillDepthStencilAttachmentDescription(info, attachment_description)) {
      implicit_transitions |= 1u << info.num_color_attachments;
    }
    depth_stencil_format_ = attachment_description->format;
    FX_DCHECK(image_utils::IsDepthFormat(depth_stencil_format_) ||
              image_utils::IsStencilFormat(depth_stencil_format_));
  }

  std::vector<vk::SubpassDescription> vk_subpass_descriptions(num_info_subpasses);
  std::vector<vk::SubpassDependency> vk_subpass_dependencies;

  // Initialize a vk::SubpassDescription for each subpass.  For each of the
  // attachment types (input, color, resolve, depth-stencil, and preserve), the
  // correct number of vk::AttachmentReferencess are allocated.  Each is
  // initialized with the proper attachment index, but their layouts are left to
  // be filled in later.
  StackAllocator<vk::AttachmentReference, 1024> reference_allocator;
  for (uint32_t i = 0; i < num_info_subpasses; i++) {
    // Allocate enough vk::AttachmentReferences for all of the
    // color/input/resolve/depth attachments used by the i-th subpass.
    auto* color_att_refs =
        reference_allocator.AllocateFilled(info_subpasses[i].num_color_attachments);
    auto* input_att_refs =
        reference_allocator.AllocateFilled(info_subpasses[i].num_input_attachments);
    auto* resolve_att_refs =
        reference_allocator.AllocateFilled(info_subpasses[i].num_color_attachments);
    auto* depth_att_ref = reference_allocator.AllocateFilled(1);

    auto& subpass = vk_subpass_descriptions[i];
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = info_subpasses[i].num_color_attachments;
    subpass.pColorAttachments = color_att_refs;
    subpass.inputAttachmentCount = info_subpasses[i].num_input_attachments;
    subpass.pInputAttachments = input_att_refs;
    subpass.pDepthStencilAttachment = depth_att_ref;

    if (info_subpasses[i].num_resolve_attachments) {
      // TODO(fxbug.dev/7174): evaluate tradeoffs of relaxing this constraint.  How often
      // would it be beneficial to resolve some subset of the attachments?  How
      // much less convenient would the API become?  e.g. what changes would
      // need to be made to the RenderPassInfo struct?
      FX_DCHECK(info_subpasses[i].num_color_attachments ==
                info_subpasses[i].num_resolve_attachments);
      subpass.pResolveAttachments = resolve_att_refs;
    }

    for (uint32_t j = 0; j < subpass.colorAttachmentCount; j++) {
      auto att = info_subpasses[i].color_attachments[j];
      FX_DCHECK(att == VK_ATTACHMENT_UNUSED || (att < num_attachments));
      color_att_refs[j].attachment = att;
      // Will be filled in below, with the value of |current_layout|.
      color_att_refs[j].layout = vk::ImageLayout::eUndefined;
    }

    for (uint32_t j = 0; j < subpass.inputAttachmentCount; j++) {
      auto att = info_subpasses[i].input_attachments[j];
      FX_DCHECK(att == VK_ATTACHMENT_UNUSED || (att < num_attachments));
      input_att_refs[j].attachment = att;
      // Will be filled in below, with the value of |current_layout|.
      input_att_refs[j].layout = vk::ImageLayout::eUndefined;
    }

    if (subpass.pResolveAttachments) {
      for (uint32_t j = 0; j < subpass.colorAttachmentCount; j++) {
        auto att = info_subpasses[i].resolve_attachments[j];
        FX_DCHECK(att == VK_ATTACHMENT_UNUSED || (att < num_attachments));
        resolve_att_refs[j].attachment = att;
        // Will be filled in below, with the value of |current_layout|.
        resolve_att_refs[j].layout = vk::ImageLayout::eUndefined;
      }
    }

    depth_att_ref->attachment =
        (has_depth_stencil_attachment &&
         info_subpasses[i].depth_stencil_mode != RenderPassInfo::DepthStencil::kNone)
            ? info.num_color_attachments
            : VK_ATTACHMENT_UNUSED;
    // To be filled in later.
    depth_att_ref->layout = vk::ImageLayout::eUndefined;
  }

  // Now, figure out how each attachment is used throughout the subpasses.
  // Either we don't care (inherit previous pass), or we need something
  // specific. Start with initial layouts.
  uint32_t preserve_masks[VulkanLimits::kNumColorAttachments + 1] = {};

  // Last subpass which makes use of an attachment.
  uint32_t last_subpass_for_attachment[VulkanLimits::kNumColorAttachments + 1] = {};

  // 1 << subpass bit set if there are color attachment self-dependencies in
  // the subpass.
  uint32_t color_self_dependencies = 0;
  // 1 << subpass bit set if there are depth-stencil attachment
  // self-dependencies in the subpass.
  uint32_t depth_self_dependencies = 0;

  // 1 << subpass bit set if any input attachment is read in the subpass.
  uint32_t input_attachment_read = 0;
  uint32_t color_attachment_read_write = 0;
  uint32_t depth_stencil_attachment_write = 0;
  uint32_t depth_stencil_attachment_read = 0;

  uint32_t external_color_dependencies = 0;
  uint32_t external_depth_dependencies = 0;
  uint32_t external_input_dependencies = 0;

  // Iterate through each attachment and:
  // - find/remember the last subpass that the attachment is used by.
  // - find the first subpass that the attachment is used by, and generate
  //   an explicit EXTERNAL subpass dependency.
  // - set the attachment's layout, depending on its usage (for example, one
  //   might be used as both a color and an input attachment, and this has
  //   implications for the set of allowable layouts).
  // - avoid gratuitous layout transitions if no "finalLayout" is specified for
  //   the attachment: simply use the layout of the attachment from the last
  //   subpass it appears in.
  for (uint32_t attachment = 0; attachment < num_attachments; attachment++) {
    // Track whether an attachment has been used within any subpass so far.
    bool used = false;
    auto current_layout = attachments[attachment].initialLayout;
    for (uint32_t subpass = 0; subpass < num_info_subpasses; subpass++) {
      auto vk_subpass_desc = &vk_subpass_descriptions[subpass];
      auto* color = FindColorAttachmentRef(vk_subpass_desc, attachment);
      auto* resolve = FindResolveAttachmentRef(vk_subpass_desc, attachment);
      auto* input = FindInputAttachmentRef(vk_subpass_desc, attachment);
      auto* depth = FindDepthStencilAttachmentRef(vk_subpass_desc, attachment);

      // Sanity check.
      if (color || resolve) {
        FX_DCHECK(!depth);
        FX_DCHECK(!color || !resolve);
      }

      // If the attachment was not used in this subpass, but it was used in a
      // previous subpass, then preserve its contents in case it is required by
      // a subsequent subpass.
      if (!color && !input && !depth && !resolve) {
        if (used) {
          // NOTE: this is overly conservative because we may be preserving an
          // attachment that isn't used in any subsequent passes.  This is
          // addressed later, by zeroing bits corresponding to subpasses after
          // the last subpass that the attachment is used.
          preserve_masks[attachment] |= 1u << subpass;
        }
        continue;
      }
      last_subpass_for_attachment[attachment] = subpass;

      if (!used && (implicit_transitions & (1u << attachment))) {
        // This is the first subpass we need implicit transitions.
        if (color)
          external_color_dependencies |= 1u << subpass;
        if (depth)
          external_depth_dependencies |= 1u << subpass;
        if (input)
          external_input_dependencies |= 1u << subpass;
        // NOTE: |resolve| isn't considered because it always depends on a
        // previous |color| attachment.
      }
      used = true;

      if (resolve) {
        // No particular layout preference.
        if (current_layout != vk::ImageLayout::eGeneral) {
          current_layout = vk::ImageLayout::eColorAttachmentOptimal;
        }
        resolve->layout = current_layout;
        color_attachment_read_write |= 1u << subpass;
      } else if (color && input) {
        // If used as both input attachment and color attachment in same
        // subpass, layout must be eGeneral.
        current_layout = vk::ImageLayout::eGeneral;
        color->layout = current_layout;
        input->layout = current_layout;
        color_self_dependencies |= 1u << subpass;
        color_attachment_read_write |= 1u << subpass;
        input_attachment_read |= 1u << subpass;
      } else if (color) {
        // No particular layout preference.
        if (current_layout != vk::ImageLayout::eGeneral) {
          current_layout = vk::ImageLayout::eColorAttachmentOptimal;
        }
        color->layout = current_layout;
        color_attachment_read_write |= 1u << subpass;
      } else if (depth && input) {
        // Layout depends on the depth mode.
        FX_DCHECK(info_subpasses[subpass].depth_stencil_mode !=
                  RenderPassInfo::DepthStencil::kNone);
        if (info_subpasses[subpass].depth_stencil_mode ==
            RenderPassInfo::DepthStencil::kReadWrite) {
          // If used as both input attachment and writable depth attachment in
          // same subpass, layout must be eGeneral.
          current_layout = vk::ImageLayout::eGeneral;
          depth_self_dependencies |= 1u << subpass;
          depth_stencil_attachment_write |= 1u << subpass;
        } else if (current_layout != vk::ImageLayout::eGeneral) {
          // No particular layout preference; might as well use the optimal.
          current_layout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
        }
        depth->layout = current_layout;
        input->layout = current_layout;
        depth_stencil_attachment_read |= 1u << subpass;
        input_attachment_read |= 1u << subpass;
      } else if (depth) {
        // Layout depends on depth mode.
        if (info_subpasses[subpass].depth_stencil_mode ==
            RenderPassInfo::DepthStencil::kReadWrite) {
          if (current_layout != vk::ImageLayout::eGeneral) {
            // No particular layout preference; might as well use the optimal.
            current_layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
          }
          depth_stencil_attachment_write |= 1u << subpass;
        } else if (current_layout != vk::ImageLayout::eGeneral) {
          // No particular layout preference; might as well use the optimal.
          current_layout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
        }
        depth->layout = current_layout;
        depth_stencil_attachment_read |= 1u << subpass;
      } else if (input) {
        // No particular layout preference.
        if (current_layout != vk::ImageLayout::eGeneral) {
          current_layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        }

        // If the attachment is first used as an input attachment, the initial
        // layout should actually be vk::ImageLayout::eShaderReadOnlyOptimal.
        if (!used &&
            attachments[attachment].initialLayout == vk::ImageLayout::eColorAttachmentOptimal) {
          attachments[attachment].initialLayout = current_layout;
        }

        input->layout = current_layout;
      } else {
        FX_DCHECK(false) << "Unhandled attachment usage.";
      }
    }
    FX_DCHECK(used) << "attachment[" << attachment << "] was not used in any subpass.";

    // If we don't have a specific layout we need to end up in, use the last one
    // to avoid unnecessary layout transitions.
    if (attachments[attachment].finalLayout == vk::ImageLayout::eUndefined) {
      FX_DCHECK(current_layout != vk::ImageLayout::eUndefined);
      attachments[attachment].finalLayout = current_layout;
    }
  }

  // Preserve attachments between subpasses.
  StackAllocator<uint32_t, 1024> preserve_allocator;
  for (uint32_t attachment = 0; attachment < num_attachments; attachment++) {
    // As mentioned above, do not preserve attachments beyond the last subpass
    // where they are used.
    // TODO(fxbug.dev/7174): add ClearBitsAtAndAboveIndex() to bit_ops.h
    preserve_masks[attachment] &= (1u << last_subpass_for_attachment[attachment]) - 1;
  }
  for (uint32_t subpass_index = 0; subpass_index < num_info_subpasses; subpass_index++) {
    // Count the number of attachments to be preserved in this subpass.
    uint32_t preserve_count = 0;
    for (uint32_t attachment = 0; attachment < num_attachments; attachment++) {
      if (preserve_masks[attachment] & (1u << subpass_index)) {
        preserve_count++;
      }
    }

    // Allocate and write attachment preservation info.
    auto* preserve = preserve_allocator.AllocateFilled(preserve_count);
    for (uint32_t attachment = 0; attachment < num_attachments; attachment++) {
      if (preserve_masks[attachment] & (1u << subpass_index)) {
        *preserve++ = attachment;
      }
    }
    auto& subpass = vk_subpass_descriptions[subpass_index];
    subpass.pPreserveAttachments = preserve;
    subpass.preserveAttachmentCount = preserve_count;
  }

  // Add external subpass dependencies.
  //
  // TODO(fxbug.dev/7174): Section 7.1 of the Vulkan spec ("Render Pass Creation") states
  // that external subpass dependencies are implicitly specified when not given
  // explicitly by the user.  Such implicit dependencies use conservative stage
  // and access masks.  It is easy to do better for external "src" dependencies,
  // as we do below.  However, external "dst" dependencies are more difficult,
  // because we may not know whether the output will be presented to the display
  // vs. sampled as a texture.  Revisit this at some point to decide whether we
  // can be more efficient (perhaps this can be addressed as part of the future
  // "frame graph" design).
  ForEachBitIndex(
      external_color_dependencies | external_depth_dependencies | external_input_dependencies,
      [&](uint32_t subpass_index) {
        vk_subpass_dependencies.emplace_back();
        auto& dep = vk_subpass_dependencies.back();
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = subpass_index;

        if (external_color_dependencies & (1u << subpass_index)) {
          dep.srcStageMask |= vk::PipelineStageFlagBits::eColorAttachmentOutput;
          dep.dstStageMask |= vk::PipelineStageFlagBits::eColorAttachmentOutput;
          dep.srcAccessMask |= vk::AccessFlagBits::eColorAttachmentWrite;
          dep.dstAccessMask |=
              vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
        }

        if (external_depth_dependencies & (1u << subpass_index)) {
          dep.srcStageMask |= vk::PipelineStageFlagBits::eEarlyFragmentTests |
                              vk::PipelineStageFlagBits::eLateFragmentTests;
          dep.dstStageMask |= vk::PipelineStageFlagBits::eEarlyFragmentTests |
                              vk::PipelineStageFlagBits::eLateFragmentTests;
          dep.srcAccessMask |= vk::AccessFlagBits::eDepthStencilAttachmentWrite;
          dep.dstAccessMask |= vk::AccessFlagBits::eDepthStencilAttachmentWrite |
                               vk::AccessFlagBits::eDepthStencilAttachmentRead;
        }

        if (external_input_dependencies & (1u << subpass_index)) {
          dep.srcStageMask |= vk::PipelineStageFlagBits::eColorAttachmentOutput;
          dep.dstStageMask |= vk::PipelineStageFlagBits::eFragmentShader;
          dep.srcAccessMask |= vk::AccessFlagBits::eColorAttachmentWrite |
                               vk::AccessFlagBits::eDepthStencilAttachmentWrite;
          dep.dstAccessMask |= vk::AccessFlagBits::eInputAttachmentRead;
        }
      });

  // We previously identified subpasses where an input attachment depends on
  // color/depth data generated by a previous subpass.  For each of these, we
  // now generate the necessary VkSubpassDependency structs.
  ForEachBitIndex(color_self_dependencies | depth_self_dependencies, [&](uint32_t subpass) {
    vk_subpass_dependencies.emplace_back();
    auto& dep = vk_subpass_dependencies.back();
    dep.srcSubpass = subpass;
    dep.dstSubpass = subpass;
    dep.dependencyFlags = vk::DependencyFlagBits::eByRegion;

    if (color_self_dependencies & (1u << subpass)) {
      dep.srcStageMask |= vk::PipelineStageFlagBits::eColorAttachmentOutput;
      dep.srcAccessMask |= vk::AccessFlagBits::eColorAttachmentWrite;
    }

    if (depth_self_dependencies & (1u << subpass)) {
      dep.srcStageMask |= vk::PipelineStageFlagBits::eEarlyFragmentTests |
                          vk::PipelineStageFlagBits::eLateFragmentTests;
      dep.srcAccessMask |= vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    }

    dep.dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;
    dep.dstAccessMask = vk::AccessFlagBits::eInputAttachmentRead;
  });

  // Flush and invalidate caches between each subpass.
  for (uint32_t subpass = 1; subpass < num_info_subpasses; subpass++) {
    vk_subpass_dependencies.emplace_back();
    auto& dep = vk_subpass_dependencies.back();
    dep.srcSubpass = subpass - 1;
    dep.dstSubpass = subpass;
    dep.dependencyFlags = vk::DependencyFlagBits::eByRegion;

    if (color_attachment_read_write & (1u << (subpass - 1))) {
      dep.srcStageMask |= vk::PipelineStageFlagBits::eColorAttachmentOutput;
      dep.srcAccessMask |= vk::AccessFlagBits::eColorAttachmentWrite;
    }

    if (depth_stencil_attachment_write & (1u << (subpass - 1))) {
      dep.srcStageMask |= vk::PipelineStageFlagBits::eEarlyFragmentTests |
                          vk::PipelineStageFlagBits::eLateFragmentTests;
      dep.srcAccessMask |= vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    }

    if (color_attachment_read_write & (1u << subpass)) {
      dep.dstStageMask |= vk::PipelineStageFlagBits::eColorAttachmentOutput;
      dep.dstAccessMask |=
          vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eColorAttachmentRead;
    }

    if (depth_stencil_attachment_read & (1u << subpass)) {
      dep.dstStageMask |= vk::PipelineStageFlagBits::eEarlyFragmentTests |
                          vk::PipelineStageFlagBits::eLateFragmentTests;
      dep.dstAccessMask |= vk::AccessFlagBits::eDepthStencilAttachmentRead;
    }

    if (depth_stencil_attachment_write & (1u << subpass)) {
      dep.dstStageMask |= vk::PipelineStageFlagBits::eEarlyFragmentTests |
                          vk::PipelineStageFlagBits::eLateFragmentTests;
      dep.dstAccessMask |= vk::AccessFlagBits::eDepthStencilAttachmentWrite |
                           vk::AccessFlagBits::eDepthStencilAttachmentRead;
    }

    if (input_attachment_read & (1u << subpass)) {
      dep.dstStageMask |= vk::PipelineStageFlagBits::eFragmentShader;
      dep.dstAccessMask |= vk::AccessFlagBits::eInputAttachmentRead;
    }
  }

  // Store the important subpass information for later.
  for (auto& subpass : vk_subpass_descriptions) {
    SubpassInfo subpass_info = {};
    subpass_info.num_color_attachments = subpass.colorAttachmentCount;
    subpass_info.num_input_attachments = subpass.inputAttachmentCount;
    subpass_info.depth_stencil_attachment = *subpass.pDepthStencilAttachment;

    // Avoid calling memcpy on nullptr if the number of attachments is 0.
    if (subpass.colorAttachmentCount > 0) {
      memcpy(subpass_info.color_attachments, subpass.pColorAttachments,
             subpass.colorAttachmentCount * sizeof(*subpass.pColorAttachments));
    }
    if (subpass.inputAttachmentCount > 0) {
      memcpy(subpass_info.input_attachments, subpass.pInputAttachments,
             subpass.inputAttachmentCount * sizeof(*subpass.pInputAttachments));
    }

    uint32_t samples = 0;
    for (uint32_t i = 0; i < subpass_info.num_color_attachments; i++) {
      if (subpass_info.color_attachments[i].attachment == VK_ATTACHMENT_UNUSED) {
        continue;
      }

      uint32_t samp = SampleCountFlagBitsToInt(
          attachments[subpass_info.color_attachments[i].attachment].samples);
      if (samples && (samp != samples)) {
        FX_DCHECK(samp == samples);
      }
      samples = samp;
    }

    if (subpass_info.depth_stencil_attachment.attachment != VK_ATTACHMENT_UNUSED) {
      uint32_t samp = SampleCountFlagBitsToInt(
          attachments[subpass_info.depth_stencil_attachment.attachment].samples);
      if (samples && (samp != samples)) {
        FX_DCHECK(samp == samples);
      }
      samples = samp;
    }

    FX_DCHECK(samples > 0);
    subpass_info.samples = impl::SampleCountFlagBitsFromInt(samples);
    subpasses_.push_back(subpass_info);
  }

  // Set |color_final_layouts_| and |depth_stencil_final_layout_| for later
  // update of image attachment layouts in CommandQueue::BeginRenderPass.
  for (uint32_t attachment = 0; attachment < info.num_color_attachments; ++attachment) {
    FX_DCHECK(attachments[attachment].finalLayout != vk::ImageLayout::eUndefined);
    color_final_layouts_[attachment] = attachments[attachment].finalLayout;
  }
  if (has_depth_stencil_attachment) {
    auto idx_depth_stencil_attachment = info.num_color_attachments;
    FX_DCHECK(attachments[idx_depth_stencil_attachment].finalLayout != vk::ImageLayout::eUndefined);
    depth_stencil_final_layout_ = attachments[idx_depth_stencil_attachment].finalLayout;
  }

  // Finally!  Build the render pass!!
  vk::RenderPassCreateInfo render_pass_create_info;
  render_pass_create_info.subpassCount = num_info_subpasses;
  render_pass_create_info.pSubpasses = vk_subpass_descriptions.data();
  render_pass_create_info.pAttachments = attachments;
  render_pass_create_info.attachmentCount = num_attachments;
  render_pass_create_info.dependencyCount = static_cast<uint32_t>(vk_subpass_dependencies.size());
  render_pass_create_info.pDependencies =
      vk_subpass_dependencies.empty() ? nullptr : vk_subpass_dependencies.data();
  render_pass_ = ESCHER_CHECKED_VK_RESULT(vk_device().createRenderPass(render_pass_create_info));
}

bool RenderPass::SubpassHasDepth(uint32_t subpass) const {
  FX_DCHECK(subpass < subpasses_.size());
  return subpasses_[subpass].depth_stencil_attachment.attachment != VK_ATTACHMENT_UNUSED &&
         image_utils::IsDepthFormat(depth_stencil_format_);
}

bool RenderPass::SubpassHasStencil(uint32_t subpass) const {
  FX_DCHECK(subpass < subpasses_.size());
  return subpasses_[subpass].depth_stencil_attachment.attachment != VK_ATTACHMENT_UNUSED &&
         image_utils::IsStencilFormat(depth_stencil_format_);
}

RenderPass::~RenderPass() { vk_device().destroyRenderPass(render_pass_); }

}  // namespace impl
}  // namespace escher
