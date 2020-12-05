// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_EXAMPLES_VKPROTO_COMMON_FIXED_FUNCTIONS_H_
#define SRC_GRAPHICS_EXAMPLES_VKPROTO_COMMON_FIXED_FUNCTIONS_H_

#include <vulkan/vulkan.hpp>

namespace vkp {

class FixedFunctions {
 public:
  FixedFunctions(const vk::Extent2D &extent);
  FixedFunctions() = delete;

  vk::PipelineColorBlendAttachmentState &color_blend_attachment_info() {
    return color_blend_attachment_info_;
  }

  vk::PipelineColorBlendStateCreateInfo &color_blending_info() { return color_blending_info_; }

  vk::Extent2D &extent() { return extent_; }

  vk::PipelineInputAssemblyStateCreateInfo &input_assembly_info() { return input_assembly_info_; }

  vk::PipelineMultisampleStateCreateInfo &multisample_info() { return multisample_info_; }

  vk::PipelineRasterizationStateCreateInfo &rasterizer_info() { return rasterizer_info_; }

  vk::Rect2D &scissor() { return scissor_; }

  const vk::PipelineVertexInputStateCreateInfo &vertex_input_info() { return vertex_input_info_; }

  vk::Viewport &viewport() { return viewport_; }

  vk::PipelineViewportStateCreateInfo &viewport_info() { return viewport_info_; }

 private:
  vk::PipelineColorBlendAttachmentState color_blend_attachment_info_;
  vk::PipelineColorBlendStateCreateInfo color_blending_info_;
  vk::Extent2D extent_;
  vk::PipelineInputAssemblyStateCreateInfo input_assembly_info_;
  vk::PipelineMultisampleStateCreateInfo multisample_info_;
  vk::PipelineRasterizationStateCreateInfo rasterizer_info_;
  vk::Rect2D scissor_;
  vk::PipelineVertexInputStateCreateInfo vertex_input_info_;
  vk::Viewport viewport_;
  vk::PipelineViewportStateCreateInfo viewport_info_;
};

}  // namespace vkp

#endif  // SRC_GRAPHICS_EXAMPLES_VKPROTO_COMMON_FIXED_FUNCTIONS_H_
