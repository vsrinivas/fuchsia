// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_VULKAN_GRAPHICS_PIPELINE_H_
#define SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_VULKAN_GRAPHICS_PIPELINE_H_

#include "src/lib/fxl/macros.h"
#include "vulkan_logical_device.h"
#include "vulkan_render_pass.h"

#include <vulkan/vulkan.hpp>

class VulkanGraphicsPipeline {
 public:
  VulkanGraphicsPipeline(std::shared_ptr<VulkanLogicalDevice> device, const vk::Extent2D &extent,
                         std::shared_ptr<VulkanRenderPass> render_pass);
  ~VulkanGraphicsPipeline();

  bool Init();
  const vk::Pipeline &graphics_pipeline() const;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(VulkanGraphicsPipeline);

  bool initialized_;
  std::shared_ptr<VulkanLogicalDevice> device_;
  const vk::Extent2D extent_;
  std::shared_ptr<VulkanRenderPass> render_pass_;

  vk::PipelineLayout pipeline_layout_;
  vk::Pipeline graphics_pipeline_;
};

#endif  // SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_VULKAN_GRAPHICS_PIPELINE_H_
