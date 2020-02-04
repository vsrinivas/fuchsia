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

#ifndef SRC_UI_LIB_ESCHER_THIRD_PARTY_GRANITE_VK_RENDER_PASS_INFO_H_
#define SRC_UI_LIB_ESCHER_THIRD_PARTY_GRANITE_VK_RENDER_PASS_INFO_H_

#include <cstdint>
#include <utility>
#include <vector>

#include "src/ui/lib/escher/vk/image_view.h"
#include "src/ui/lib/escher/vk/vulkan_limits.h"

namespace escher {

// Structure passed to CommandBuffer::BeginRenderPass(), which free Escher users
// from direct exposure to VkFramebuffers and VkRenderPasses, which are managed
// behind the scenes by CommandBuffer.
//
// Creating a Vulkan render pass is an excruciatingly explicit process.
// RenderPassInfo strikes a balance between ease-of-use and efficiency;
// despite being relatively easy to use, it nevertheless remains flexible and
// sufficiently detailed to generate an efficient VkRenderPass.
//
// NOTE: the semantics of RenderPassInfo are not set in stone.  If future
// use-cases are limited by some of the current simplifications (e.g. single
// depth attachment, layout flags apply to all layouts of that type, etc.),
// appropriate adjustments can be made.
struct RenderPassInfo {
  enum OpFlagBits {
    // Clear the depth/stencil image before it is used in the render pass.
    kClearDepthStencilOp = 1 << 0,
    // Use the existing contents of the depth/stencil image in the render pass.
    kLoadDepthStencilOp = 1 << 1,
    // Store the contents of the depth/stencil image when the render pass is
    // finished.
    kStoreDepthStencilOp = 1 << 2,
    // Use most efficient layout for blending fragments into color attachments.
    // Does not apply to attachment images that have a swapchain_layout()
    // specified.
    kOptimalColorLayoutOp = 1 << 3,
    // Use most efficient for read/write depth/stencil attachment.
    kOptimalDepthStencilLayoutOp = 1 << 4,
    // Use most efficient for read-only depth/stencil attachment.
    kDepthStencilReadOnlyLayoutOp = 1 << 5
  };
  using OpFlags = uint32_t;

  // Describes how the depth-stencil attachment is used in each subpass.
  enum class DepthStencil { kNone, kReadOnly, kReadWrite };

  struct Subpass {
    // These are indices into the RenderPassInfo's list of color attachments,
    // which indicate which are used in this subpass, and how they are used.
    uint32_t color_attachments[VulkanLimits::kNumColorAttachments];
    uint32_t input_attachments[VulkanLimits::kNumColorAttachments];
    uint32_t resolve_attachments[VulkanLimits::kNumColorAttachments];

    // These indicate the number of color/input/resolve attachments that are
    // active in this subpass.  For example, if |num_resolve_attachments| is 2
    // then only the first two indices in |resolve_attachments| are considered;
    // the rest are ignored.
    uint32_t num_color_attachments = 0;
    uint32_t num_input_attachments = 0;
    uint32_t num_resolve_attachments = 0;

    DepthStencil depth_stencil_mode = DepthStencil::kReadWrite;
  };

  // This is a helper struct used for creating RenderPasses without needing ImageViews for the depth
  // and color attachments.  Most client code won't need to use this, since Pipeline/RenderPass
  // management is entirely encapsulated within CommandBuffer... almost.  The exception is when
  // "warming the caches" to avoid lazy and Pipeline/RenderPass generation.  This struct helps to
  // allow eager Pipeline/RenderPass creation without a dependency on CommandBuffer.
  struct AttachmentInfo {
    vk::Format format = vk::Format::eUndefined;
    vk::ImageLayout swapchain_layout = vk::ImageLayout::eUndefined;
    uint32_t sample_count = 1;
    bool is_transient = false;

    bool is_swapchain_image() const { return swapchain_layout != vk::ImageLayout::eUndefined; }

    // Uses |image| to populate all fields: format/swapchain_layout/sample_count/is_transient.
    void InitFromImage(const ImagePtr& image);
  };

  // An optional depth-stencil attachment to be used by this render pass.
  //
  // NOTE: There may be use-cases where having multiple depth attachments is
  // desirable.  This is not currently supported, but support can be added if
  // necessary.
  ImageViewPtr depth_stencil_attachment;
  AttachmentInfo depth_stencil_attachment_info;

  // Array of all of the color attachments that are used in this render pass.
  // Only the first |num_color_attachments| values are considered; the rest are
  // ignored.  In general, not all attachments will be used in each subpass;
  // the |Subpass| struct above describes which attachments are used.
  ImageViewPtr color_attachments[VulkanLimits::kNumColorAttachments];
  std::array<AttachmentInfo, VulkanLimits::kNumColorAttachments> color_attachment_infos;
  uint32_t num_color_attachments = 0;
  RenderPassInfo::OpFlags op_flags = 0;

  // Bits that describe the clear/load/store behavior for each of the images
  // in |color_attachments|; the bit for a particular attachment index is given
  // by "1u << index".
  uint32_t clear_attachments = 0;
  uint32_t load_attachments = 0;
  uint32_t store_attachments = 0;

  // Render area will be clipped to the actual framebuffer.
  vk::Rect2D render_area = {{0, 0}, {UINT32_MAX, UINT32_MAX}};

  vk::ClearColorValue clear_color[VulkanLimits::kNumColorAttachments] = {};
  vk::ClearDepthStencilValue clear_depth_stencil = {1.0f, 0};

  // If empty, a default subpass will be provided.
  std::vector<Subpass> subpasses;

  // Runs a series of validity checks on the RenderPassInfo, returning true only if all pass.
  // For example:
  // - there must be at least one attachment (either color or depth-stencil).
  // - the same attachment cannot be both loaded and cleared.
  bool Validate() const;

  // Return appropriate load/store ops for the specified color attachment,
  // depending on which of the corresponding bits are set in:
  // - clear_attachments
  // - load_attachments
  // - store attachments
  // Convenience method, primarily used by the impl::RenderPass constructor.
  std::pair<vk::AttachmentLoadOp, vk::AttachmentStoreOp> LoadStoreOpsForColorAttachment(
      uint32_t index) const;

  // Return appropriate load/store ops, depending on which of these flags are
  // set:
  // - kClearDepthStencilOp
  // - kLoadDepthStencilOp
  // - kStoreDepthStencilOp
  // Convenience method, primarily used by the impl::RenderPass constructor.
  std::pair<vk::AttachmentLoadOp, vk::AttachmentStoreOp> LoadStoreOpsForDepthStencilAttachment()
      const;

  // Handles the logic for setting up a vulkan render pass. If there are MSAA buffers a resolve
  // subpass is also added. Clear color is set to transparent-black and if the frame has a depth
  // texture that will also be used. This is general enough to meet most standard needs but if a
  // client wants something that is not handled here they will have to manually initialize their
  // own RenderPassInfo struct.
  //
  // This variant of InitRenderPassInfo() is used when the application wants to begin a render pass,
  // which requires the attachment images to be provided.
  static void InitRenderPassInfo(RenderPassInfo* rp, vk::Rect2D render_area,
                                 const ImagePtr& output_image, const TexturePtr& depth_texture,
                                 const TexturePtr& msaa_texture = nullptr,
                                 ImageViewAllocator* allocator = nullptr);
  // This variant of InitRenderPassInfo() cannot be used to call CommandBuffer::BeginRenderPass(),
  // because there are no attachment images provided.  It can be used for warming up the render-pass
  // cache to avoid jank caused by creating render-passes/pipelines at runtime.
  //
  // |msaa_format| will be ignored if |sample_count| == 1.
  static void InitRenderPassInfo(RenderPassInfo* rp,
                                 const RenderPassInfo::AttachmentInfo& color_info,
                                 vk::Format depth_stencil_format, vk::Format msaa_format,
                                 uint32_t sample_count, bool use_transient_depth_and_msaa);

  // Used to reduce boilerplate when InitRenderPassInfo() is too generic.  This helper populates
  // |depth_stencil_attachment_info| and |color_attachment_infos| with information from the images
  // |depth_stencil_attachment| and |color_attachments|.  The caller is responsible for filling in
  // all other fields.
  //
  // TODO(44566): this is mostly used for tests, some (all?) of which can be rewritten to use the
  // "no images" variant of InitRenderPassInfo.  Aim for deletion of this function.
  static void InitRenderPassAttachmentInfosFromImages(RenderPassInfo* rp);
};

ESCHER_DEBUG_PRINTABLE(RenderPassInfo);
ESCHER_DEBUG_PRINTABLE(RenderPassInfo::AttachmentInfo);

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_THIRD_PARTY_GRANITE_VK_RENDER_PASS_INFO_H_
