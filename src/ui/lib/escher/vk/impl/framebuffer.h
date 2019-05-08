// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_VK_IMPL_FRAMEBUFFER_H_
#define SRC_UI_LIB_ESCHER_VK_IMPL_FRAMEBUFFER_H_

#include <vulkan/vulkan.hpp>

#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/third_party/granite/vk/render_pass.h"
#include "src/ui/lib/escher/vk/render_pass_info.h"

namespace escher {
namespace impl {

// Wraps a Vulkan framebuffer object, and makes available the corresponding
// Vulkan render pass.
class Framebuffer : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  Framebuffer(ResourceRecycler* recycler, RenderPassPtr pass,
              const RenderPassInfo& pass_info);
  ~Framebuffer() override;

  vk::Framebuffer vk() const { return framebuffer_; }

  // In Vulkan, framebuffers and render passes are tightly coupled concepts;
  // this is reflected by making the render pass available here, for convenient
  // access by CommandBuffer/ShaderProgram, which use it to generate appropriate
  // VkPipelines.
  vk::RenderPass vk_render_pass() const { return render_pass_->vk(); }
  const RenderPassPtr& render_pass() const { return render_pass_; }
  const RenderPassInfo& render_pass_info() const { return render_pass_info_; }

  // Get the color or depth-stencil attachment identified by |index|.  The color
  // attachments indices are [0, render_pass_info().num_color_attachments - 1],
  // and the index of the depth-stencil attachment (if any) is one greater than
  // the highest color attachment index.
  const ImageViewPtr& GetAttachment(uint32_t index) {
    FXL_DCHECK(index <
               render_pass_info_.num_color_attachments +
                   (render_pass_info_.depth_stencil_attachment ? 1 : 0));
    return index < render_pass_info_.num_color_attachments
               ? render_pass_info_.color_attachments[index]
               : render_pass_info_.depth_stencil_attachment;
  }

  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }
  vk::Extent2D extent() const { return {width_, height_}; }

 private:
  vk::Framebuffer framebuffer_;
  impl::RenderPassPtr render_pass_;
  RenderPassInfo render_pass_info_;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
};

}  // namespace impl
}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_VK_IMPL_FRAMEBUFFER_H_
