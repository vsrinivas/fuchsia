// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_VK_RENDER_PASS_H_
#define SRC_UI_LIB_ESCHER_VK_RENDER_PASS_H_

#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/resources/resource.h"

namespace escher {

class RenderPass;
typedef fxl::RefPtr<RenderPass> RenderPassPtr;

// Escher's standard interface to Vulkan render pass objects.
// TODO(fxbug.dev/7174): deprecated.  Render passes will soon be handled transparently
// by the new CommandBuffer object.
class RenderPass : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  // Takes ownership of the render pass.
  RenderPass(ResourceManager* manager, vk::RenderPass render_pass);

  ~RenderPass() override;

  // Return the underlying Vulkan render pass object.
  vk::RenderPass vk() {
    FX_DCHECK(render_pass_);
    return render_pass_;
  }

  // Returns the info that was used to create the underlying Vulkan render pass.
  const vk::RenderPassCreateInfo& create_info() const { return create_info_; }

 protected:
  // This constructor initializes |create_info_| to point at the attachments,
  // subpasses, and subpass-dependencies.  The subclass is responsible for
  // calling CreateRenderPass() before returning from its constructor.
  RenderPass(ResourceManager* manager, uint32_t color_attachment_count,
             uint32_t depth_attachment_count, uint32_t attachment_reference_count,
             uint32_t subpass_count, uint32_t subpass_dependency_count);

  // Called by subclasses after all subpasses/attachments/etc. have been set up.
  void CreateRenderPass();

  vk::AttachmentDescription* color_attachment(uint32_t index) {
    FX_DCHECK(index < color_attachment_count_);
    return &attachments_[index];
  }

  vk::AttachmentDescription* depth_attachment(uint32_t index) {
    FX_DCHECK(index < depth_attachment_count_);
    return &attachments_[color_attachment_count_ + index];
  }

  vk::AttachmentDescription* attachment(uint32_t index) {
    FX_DCHECK(index < attachments_.size());
    return &attachments_[index];
  }

  const vk::AttachmentDescription* attachment(uint32_t index) const {
    FX_DCHECK(index < attachments_.size());
    return &attachments_[index];
  }

  uint32_t color_attachment_index(uint32_t index) {
    FX_DCHECK(index < color_attachment_count_);
    return index;
  }

  uint32_t depth_attachment_index(uint32_t index) {
    FX_DCHECK(index < depth_attachment_count_);
    return color_attachment_count_ + index;
  }

  vk::AttachmentReference* attachment_reference(uint32_t index) {
    FX_DCHECK(index < attachment_references_.size());
    return &attachment_references_[index];
  }

  vk::SubpassDescription* subpass_description(uint32_t index) {
    FX_DCHECK(index < subpass_descriptions_.size());
    return &subpass_descriptions_[index];
  }

  vk::SubpassDependency* subpass_dependency(uint32_t index) {
    FX_DCHECK(index < subpass_dependencies_.size());
    return &subpass_dependencies_[index];
  }

 private:
  // Underlying Vulkan render pass object, and the info used to create it.
  vk::RenderPass render_pass_;
  vk::RenderPassCreateInfo create_info_;

  const uint32_t color_attachment_count_;
  const uint32_t depth_attachment_count_;
  std::vector<vk::AttachmentDescription> attachments_;
  std::vector<vk::AttachmentReference> attachment_references_;
  std::vector<vk::SubpassDescription> subpass_descriptions_;
  std::vector<vk::SubpassDependency> subpass_dependencies_;
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_VK_RENDER_PASS_H_
