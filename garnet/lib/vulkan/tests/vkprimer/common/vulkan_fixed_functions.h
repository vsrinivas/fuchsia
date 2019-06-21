// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_FIXED_FUNCTIONS_H_
#define GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_FIXED_FUNCTIONS_H_

#include "vulkan/vulkan.h"

class VulkanFixedFunctions {
 public:
  VulkanFixedFunctions(const VkExtent2D &extent);
  VulkanFixedFunctions() = delete;

  const VkPipelineColorBlendAttachmentState &color_blend_attachment_info() {
    return color_blend_attachment_info_;
  }

  const VkPipelineColorBlendStateCreateInfo &color_blending_info() {
    return color_blending_info_;
  }

  const VkExtent2D &extent() { return extent_; }

  const VkPipelineInputAssemblyStateCreateInfo &input_assembly_info() {
    return input_assembly_info_;
  }

  const VkPipelineMultisampleStateCreateInfo &multisample_info() {
    return multisample_info_;
  }

  const VkPipelineRasterizationStateCreateInfo &rasterizer_info() {
    return rasterizer_info_;
  }

  const VkRect2D &scissor() { return scissor_; }

  const VkPipelineVertexInputStateCreateInfo &vertex_input_info() {
    return vertex_input_info_;
  }

  const VkViewport &viewport() { return viewport_; }

  const VkPipelineViewportStateCreateInfo &viewport_info() {
    return viewport_info_;
  }

 private:
  const VkPipelineColorBlendAttachmentState color_blend_attachment_info_ = {
      .blendEnable = VK_FALSE,
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
  };

  const VkPipelineColorBlendStateCreateInfo color_blending_info_ = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &color_blend_attachment_info_,
      .blendConstants = {0, 0, 0, 0},
      .logicOp = VK_LOGIC_OP_COPY,
      .logicOpEnable = VK_FALSE,
  };

  const VkExtent2D extent_;

  const VkPipelineInputAssemblyStateCreateInfo input_assembly_info_ = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .primitiveRestartEnable = VK_FALSE,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
  };

  const VkPipelineMultisampleStateCreateInfo multisample_info_ = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
      .sampleShadingEnable = VK_FALSE,
  };

  const VkPipelineRasterizationStateCreateInfo rasterizer_info_ = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .cullMode = VK_CULL_MODE_BACK_BIT,
      .depthBiasEnable = VK_FALSE,
      .depthClampEnable = VK_FALSE,
      .frontFace = VK_FRONT_FACE_CLOCKWISE,
      .lineWidth = 1.0f,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .rasterizerDiscardEnable = VK_FALSE,
  };

  VkRect2D scissor_ = {
      .offset = {0, 0},
  };

  const VkPipelineVertexInputStateCreateInfo vertex_input_info_ = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexAttributeDescriptionCount = 0,
      .vertexBindingDescriptionCount = 0,
  };

  VkViewport viewport_ = {
      .maxDepth = 1.0f,
      .minDepth = 0.0f,
      .x = 0.0f,
      .y = 0.0f,
  };

  const VkPipelineViewportStateCreateInfo viewport_info_ = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .scissorCount = 1,
      .pScissors = &scissor_,
      .viewportCount = 1,
      .pViewports = &viewport_,
  };
};

#endif  // GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_FIXED_FUNCTIONS_H_
