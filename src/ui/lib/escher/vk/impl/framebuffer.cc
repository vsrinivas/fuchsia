// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/impl/framebuffer.h"

#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/resources/resource_recycler.h"
#include "src/ui/lib/escher/third_party/granite/vk/render_pass.h"
#include "src/ui/lib/escher/vk/vulkan_limits.h"

namespace escher {
namespace impl {

const ResourceTypeInfo impl::Framebuffer::kTypeInfo("impl::Framebuffer", ResourceType::kResource,
                                                    ResourceType::kImplFramebuffer);

Framebuffer::Framebuffer(ResourceRecycler* recycler, RenderPassPtr pass,
                         const RenderPassInfo& pass_info)
    : Resource(recycler),
      render_pass_(std::move(pass)),
      render_pass_info_(pass_info),
      width_(UINT32_MAX),
      height_(UINT32_MAX) {
  vk::ImageView views[VulkanLimits::kNumColorAttachments + 1];
  uint32_t num_views = 0;

  FX_DCHECK(pass_info.num_color_attachments || pass_info.depth_stencil_attachment);

  // TODO(fxbug.dev/7172): handle LOD (whatever that means, precisely).  Perhaps LOD
  // should be added explicitly so that e.g. if Scenic wants to render a
  // hypothetical ScreenResource, a LOD can be provided depending of the scale
  // that the Screen will be rendered at in the Scene.  OTOH, in that case
  // perhaps the better approach would be to populate the RenderPassInfo with
  // attachment ImageViews that reflect the desired LOD.
  for (uint32_t i = 0; i < pass_info.num_color_attachments; i++) {
    FX_DCHECK(pass_info.color_attachments[i]);
    width_ = std::min(width_, pass_info.color_attachments[i]->width());
    height_ = std::min(height_, pass_info.color_attachments[i]->height());
    views[num_views++] = pass_info.color_attachments[i]->vk();
  }

  if (pass_info.depth_stencil_attachment) {
    width_ = std::min(width_, pass_info.depth_stencil_attachment->width());
    height_ = std::min(height_, pass_info.depth_stencil_attachment->height());
    views[num_views++] = pass_info.depth_stencil_attachment->vk();
  }

  vk::FramebufferCreateInfo fb_info;
  fb_info.renderPass = render_pass_->vk();
  fb_info.attachmentCount = num_views;
  fb_info.pAttachments = views;
  fb_info.width = width_;
  fb_info.height = height_;
  fb_info.layers = 1;

  framebuffer_ = ESCHER_CHECKED_VK_RESULT(recycler->vk_device().createFramebuffer(fb_info));
}

Framebuffer::~Framebuffer() { vk_device().destroyFramebuffer(framebuffer_); }

}  // namespace impl
}  // namespace escher
