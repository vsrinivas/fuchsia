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
// - vulkan/render_pass.hpp

#ifndef SRC_UI_LIB_ESCHER_THIRD_PARTY_GRANITE_VK_RENDER_PASS_H_
#define SRC_UI_LIB_ESCHER_THIRD_PARTY_GRANITE_VK_RENDER_PASS_H_

#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/resources/resource.h"
#include "src/ui/lib/escher/vk/vulkan_limits.h"

#include <vulkan/vulkan.hpp>

namespace escher {
namespace impl {

class RenderPass;
using RenderPassPtr = fxl::RefPtr<RenderPass>;

// RenderPass encapsulates a Vulkan render pass.  Once constructed, it behaves
// as a simple container.  However, the construction process is fairly involved,
// due to the boilerplate required by Vulkan.  See the constructor comment for
// additional details, as well as the RenderPassInfo documentation.
//
// NOTE: this class is an implementation detail of CommandBuffer; Escher clients
// are never directly exposed to it.  Instead, they use RenderPassInfo.
//
// TODO(fxbug.dev/7170): RenderPass and Framebuffer are deprecated, to be replaced by
// impl::RenderPass and impl::Framebuffer.  The latter two aren't visible to
// Escher clients; they are an implementation detail of escher::CommandBuffer
// (NOTE: NOT escher::impl::CommandBuffer, which is also deprecated).
class RenderPass : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  // In order to create a VkRenderPass, Vulkan requires explicit info about e.g.
  // which attachments are used in which subpasses, which subpasses are depended
  // upon by other subpasses, which image layouts should be used by for each
  // image in each subpass, etc.  This constructor takes a RenderPassInfo as
  // input, from which it generates a VkRenderPassCreateInfo containing the
  // explicit specification required by Vulkan.  See RenderPassInfo for more
  // details.
  RenderPass(ResourceRecycler* recycler, const RenderPassInfo& info);
  ~RenderPass() override;

  struct SubpassInfo {
    vk::AttachmentReference color_attachments[VulkanLimits::kNumColorAttachments];
    uint32_t num_color_attachments;
    vk::AttachmentReference input_attachments[VulkanLimits::kNumColorAttachments];
    uint32_t num_input_attachments;
    vk::AttachmentReference depth_stencil_attachment;

    vk::SampleCountFlagBits samples;
  };

  uint32_t num_subpasses() const { return static_cast<uint32_t>(subpasses_.size()); }
  uint32_t num_color_attachments() const { return num_color_attachments_; }

  vk::RenderPass vk() const { return render_pass_; }

  vk::ImageLayout GetColorAttachmentFinalLayout(size_t index) const {
    FX_DCHECK(index < num_color_attachments_);
    return color_final_layouts_[index];
  }

  vk::ImageLayout GetDepthStencilAttachmentFinalLayout() const {
    return depth_stencil_final_layout_;
  }

  vk::SampleCountFlagBits SubpassSamples(uint32_t subpass) const;

  uint32_t GetColorAttachmentCountForSubpass(uint32_t subpass) const;
  uint32_t GetInputAttachmentCountForSubpass(uint32_t subpass) const;

  const vk::AttachmentReference& GetColorAttachmentForSubpass(uint32_t subpass,
                                                              uint32_t index) const;

  const vk::AttachmentReference& GetInputAttachmentForSubpass(uint32_t subpass,
                                                              uint32_t index) const;

  bool SubpassHasDepth(uint32_t subpass_index) const;
  bool SubpassHasStencil(uint32_t subpass_index) const;

 private:
  vk::RenderPass render_pass_;
  uint32_t num_color_attachments_ = 0;

  vk::Format color_formats_[VulkanLimits::kNumColorAttachments];
  vk::Format depth_stencil_format_;

  vk::ImageLayout color_final_layouts_[VulkanLimits::kNumColorAttachments];
  vk::ImageLayout depth_stencil_final_layout_;

  std::vector<SubpassInfo> subpasses_;
};

// Inline method definitions.

inline vk::SampleCountFlagBits RenderPass::SubpassSamples(uint32_t subpass) const {
  FX_DCHECK(subpass < subpasses_.size()) << subpass << " vs. " << subpasses_.size();
  return subpasses_[subpass].samples;
}

inline uint32_t RenderPass::GetColorAttachmentCountForSubpass(uint32_t subpass) const {
  FX_DCHECK(subpass < subpasses_.size()) << subpass << " vs. " << subpasses_.size();
  return subpasses_[subpass].num_color_attachments;
}

inline uint32_t RenderPass::GetInputAttachmentCountForSubpass(uint32_t subpass) const {
  FX_DCHECK(subpass < subpasses_.size()) << subpass << " vs. " << subpasses_.size();
  return subpasses_[subpass].num_input_attachments;
}

inline const vk::AttachmentReference& RenderPass::GetColorAttachmentForSubpass(
    uint32_t subpass, uint32_t index) const {
  FX_DCHECK(subpass < subpasses_.size()) << subpass << " vs. " << subpasses_.size();
  FX_DCHECK(index < subpasses_[subpass].num_color_attachments);
  return subpasses_[subpass].color_attachments[index];
}

inline const vk::AttachmentReference& RenderPass::GetInputAttachmentForSubpass(
    uint32_t subpass, uint32_t index) const {
  FX_DCHECK(subpass < subpasses_.size());
  FX_DCHECK(index < subpasses_[subpass].num_input_attachments);
  return subpasses_[subpass].input_attachments[index];
}

}  // namespace impl
}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_THIRD_PARTY_GRANITE_VK_RENDER_PASS_H_
