// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/impl/model_lighting_pass.h"

#include "lib/escher/impl/model_data.h"

namespace escher {
namespace impl {

ModelLightingPass::ModelLightingPass(ResourceRecycler* recycler,
                                     ModelDataPtr model_data,
                                     vk::Format color_format,
                                     vk::Format depth_format,
                                     uint32_t sample_count)
    : ModelRenderPass(recycler, color_format, depth_format, sample_count) {
  vk::AttachmentDescription* color_attachment =
      attachment(kColorAttachmentIndex);
  vk::AttachmentDescription* depth_attachment =
      attachment(kDepthAttachmentIndex);

  // Create the depth-prepass RenderPass.
  color_attachment->loadOp = vk::AttachmentLoadOp::eClear;
  // TODO: necessary to store if we resolve as part of the render-pass?
  color_attachment->storeOp = vk::AttachmentStoreOp::eStore;
  color_attachment->initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
  color_attachment->finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
  depth_attachment->loadOp = vk::AttachmentLoadOp::eClear;
  depth_attachment->storeOp = vk::AttachmentStoreOp::eDontCare;
  depth_attachment->initialLayout = vk::ImageLayout::eUndefined;
  depth_attachment->finalLayout =
      vk::ImageLayout::eDepthStencilAttachmentOptimal;

  // We have finished specifying the render-pass.  Now create it.
  CreateRenderPassAndPipelineCache(std::move(model_data));
}

}  // namespace impl
}  // namespace escher
