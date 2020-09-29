// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/render_pass.h"

namespace escher {

const ResourceTypeInfo RenderPass::kTypeInfo("RenderPass", ResourceType::kResource,
                                             ResourceType::kRenderPass);

RenderPass::RenderPass(ResourceManager* manager, vk::RenderPass render_pass)
    : Resource(manager),
      render_pass_(render_pass),
      color_attachment_count_(0),
      depth_attachment_count_(0) {
  FX_DCHECK(render_pass);
  create_info_.attachmentCount = 0;
  create_info_.pAttachments = nullptr;
  create_info_.subpassCount = 0;
  create_info_.pSubpasses = nullptr;
  create_info_.dependencyCount = 0;
  create_info_.pDependencies = nullptr;
}

RenderPass::RenderPass(ResourceManager* manager, uint32_t color_attachment_count,
                       uint32_t depth_attachment_count, uint32_t attachment_reference_count,
                       uint32_t subpass_count, uint32_t subpass_dependency_count)
    : Resource(manager),
      color_attachment_count_(color_attachment_count),
      depth_attachment_count_(depth_attachment_count) {
  FX_DCHECK(subpass_count > 0);
  attachments_.resize(color_attachment_count + depth_attachment_count);
  attachment_references_.resize(attachment_reference_count);
  subpass_descriptions_.resize(subpass_count);
  subpass_dependencies_.resize(subpass_dependency_count);

  create_info_.attachmentCount = static_cast<uint32_t>(attachments_.size());
  create_info_.pAttachments = attachments_.data();
  create_info_.subpassCount = static_cast<uint32_t>(subpass_descriptions_.size());
  create_info_.pSubpasses = subpass_descriptions_.data();
  create_info_.dependencyCount = static_cast<uint32_t>(subpass_dependencies_.size());
  create_info_.pDependencies = subpass_dependencies_.data();
}

RenderPass::~RenderPass() {
  if (render_pass_) {
    vk_device().destroyRenderPass(render_pass_);
  }
}

void RenderPass::CreateRenderPass() {
  FX_DCHECK(!render_pass_);

  auto result = vk_device().createRenderPass(create_info_);
  if (result.result == vk::Result::eSuccess) {
    render_pass_ = result.value;
  } else {
    FX_LOGS(ERROR) << "Failed to create Vulkan RenderPass.";
  }
}

}  // namespace escher
