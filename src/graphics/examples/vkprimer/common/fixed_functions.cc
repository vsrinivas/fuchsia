// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/examples/vkprimer/common/fixed_functions.h"

namespace vkp {

FixedFunctions::FixedFunctions(const vk::Extent2D &extent) : extent_(extent) {
  color_blend_attachment_info_.setColorWriteMask(
      vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
      vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);

  color_blending_info_.attachmentCount = 1;
  color_blending_info_.pAttachments = &color_blend_attachment_info_;
  color_blending_info_.logicOp = vk::LogicOp::eCopy;

  input_assembly_info_.topology = vk::PrimitiveTopology::eTriangleList;

  multisample_info_.rasterizationSamples = vk::SampleCountFlagBits::e1;

  rasterizer_info_.cullMode = vk::CullModeFlagBits::eBack;
  rasterizer_info_.frontFace = vk::FrontFace::eClockwise;
  rasterizer_info_.lineWidth = 1.0f;
  rasterizer_info_.polygonMode = vk::PolygonMode::eFill;

  scissor_.extent = extent_;

  viewport_.maxDepth = 1.0f;
  viewport_.setHeight(static_cast<float>(extent_.height));
  viewport_.setWidth(static_cast<float>(extent_.width));

  viewport_info_.scissorCount = 1;
  viewport_info_.pScissors = &scissor_;
  viewport_info_.viewportCount = 1;
  viewport_info_.pViewports = &viewport_;
};

}  // namespace vkp
