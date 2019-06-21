// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_GRAPHICS_PIPELINE_H_
#define GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_GRAPHICS_PIPELINE_H_

#include <src/lib/fxl/macros.h>

#include "vulkan/vulkan.h"
#include "vulkan_logical_device.h"

class VulkanGraphicsPipeline {
 public:
  VulkanGraphicsPipeline(std::shared_ptr<VulkanLogicalDevice> device,
                         const VkExtent2D &extent,
                         const VkRenderPass &render_pass);
  ~VulkanGraphicsPipeline();

  bool Init();

  const VkPipeline &graphics_pipeline() { return graphics_pipeline_; }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(VulkanGraphicsPipeline);

  bool initialized_;
  std::shared_ptr<VulkanLogicalDevice> device_;
  const VkExtent2D extent_;

  struct InitParams {
    InitParams(const VkRenderPass &render_pass_);
    const VkRenderPass render_pass_;
  };
  std::unique_ptr<InitParams> params_;

  VkPipelineLayout pipeline_layout_;
  VkPipeline graphics_pipeline_;
};

#endif  // GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_GRAPHICS_PIPELINE_H_
