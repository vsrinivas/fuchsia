// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_FIXED_FUNCTIONS_H_
#define GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_FIXED_FUNCTIONS_H_

#include <vulkan/vulkan.hpp>

class VulkanFixedFunctions {
 public:
  VulkanFixedFunctions(const vk::Extent2D &extent);
  VulkanFixedFunctions() = delete;

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

#endif  // GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_FIXED_FUNCTIONS_H_
