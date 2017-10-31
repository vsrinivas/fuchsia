// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/impl/model_render_pass.h"

#include "lib/escher/impl/model_data.h"
#include "lib/escher/impl/vulkan_utils.h"
#include "lib/escher/resources/resource_recycler.h"

namespace escher {
namespace impl {

static constexpr uint32_t kColorAttachmentCount = 1;
static constexpr uint32_t kDepthAttachmentCount = 1;
static constexpr uint32_t kAttachmentReferenceCount = 2;
static constexpr uint32_t kSubpassCount = 1;
static constexpr uint32_t kSubpassDependencyCount = 2;
static constexpr uint32_t kSoleSubpassIndex = 0;

ModelRenderPass::ModelRenderPass(ResourceRecycler* recycler,
                                 vk::Format color_format,
                                 vk::Format depth_format,
                                 uint32_t sample_count)
    : RenderPass(recycler,
                 kColorAttachmentCount,
                 kDepthAttachmentCount,
                 kAttachmentReferenceCount,
                 kSubpassCount,
                 kSubpassDependencyCount),
      sample_count_(sample_count) {
  // Sanity check that these indices correspond to the first color and depth
  // attachments, respectively.
  FXL_DCHECK(kColorAttachmentIndex == color_attachment_index(0));
  FXL_DCHECK(kDepthAttachmentIndex == depth_attachment_index(0));

  vk::AttachmentDescription* color_attachment =
      attachment(kColorAttachmentIndex);
  vk::AttachmentDescription* depth_attachment =
      attachment(kDepthAttachmentIndex);
  vk::AttachmentReference* color_reference =
      attachment_reference(kColorAttachmentIndex);
  vk::AttachmentReference* depth_reference =
      attachment_reference(kDepthAttachmentIndex);
  vk::SubpassDescription* single_subpass =
      subpass_description(kSoleSubpassIndex);
  vk::SubpassDependency* input_dependency = subpass_dependency(0);
  vk::SubpassDependency* output_dependency = subpass_dependency(1);

  // Common to all subclasses.
  color_attachment->format = color_format;
  color_attachment->samples = SampleCountFlagBitsFromInt(sample_count);
  depth_attachment->format = depth_format;
  depth_attachment->samples = SampleCountFlagBitsFromInt(sample_count);
  depth_attachment->stencilLoadOp = vk::AttachmentLoadOp::eClear;
  depth_attachment->stencilStoreOp = vk::AttachmentStoreOp::eDontCare;

  color_reference->attachment = kColorAttachmentIndex;
  color_reference->layout = vk::ImageLayout::eColorAttachmentOptimal;
  depth_reference->attachment = kDepthAttachmentIndex;
  depth_reference->layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

  single_subpass->pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
  single_subpass->colorAttachmentCount = 1;
  single_subpass->pColorAttachments = color_reference;
  single_subpass->pDepthStencilAttachment = depth_reference;
  // No other subpasses to sample from.
  single_subpass->inputAttachmentCount = 0;

  // The first dependency transitions from the final layout from the previous
  // render pass, to the initial layout of this one.
  input_dependency->srcSubpass = VK_SUBPASS_EXTERNAL;  // not in vulkan.hpp
  input_dependency->dstSubpass = kSoleSubpassIndex;
  input_dependency->srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
  input_dependency->dstStageMask =
      vk::PipelineStageFlagBits::eColorAttachmentOutput;
  input_dependency->srcAccessMask = vk::AccessFlagBits::eMemoryRead;
  input_dependency->dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead |
                                    vk::AccessFlagBits::eColorAttachmentWrite;
  input_dependency->dependencyFlags = vk::DependencyFlagBits::eByRegion;

  // The second dependency describes the transition from the initial to final
  // layout.
  output_dependency->srcSubpass = kSoleSubpassIndex;
  output_dependency->dstSubpass = VK_SUBPASS_EXTERNAL;
  output_dependency->srcStageMask =
      vk::PipelineStageFlagBits::eColorAttachmentOutput;
  output_dependency->dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
  output_dependency->srcAccessMask = vk::AccessFlagBits::eColorAttachmentRead |
                                     vk::AccessFlagBits::eColorAttachmentWrite;
  output_dependency->dstAccessMask = vk::AccessFlagBits::eMemoryRead;
  output_dependency->dependencyFlags = vk::DependencyFlagBits::eByRegion;
}

void ModelRenderPass::CreateRenderPassAndPipelineCache(
    ModelDataPtr model_data) {
  CreateRenderPass();
  // TODO: ModelPipelineCache doesn't need to be a resource if this render pass
  // is one.
  pipeline_cache_ = fxl::MakeRefCounted<ModelPipelineCache>(
      static_cast<ResourceRecycler*>(owner()), std::move(model_data), this);
}

}  // namespace impl
}  // namespace escher
