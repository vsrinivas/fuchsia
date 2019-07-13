// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_GRAPHICS_PIPELINE_H_
#define GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_GRAPHICS_PIPELINE_H_

#include <src/lib/fxl/macros.h>
#include <vulkan/vulkan.hpp>

#include "vulkan_logical_device.h"
#include "vulkan_render_pass.h"

class VulkanGraphicsPipeline {
 public:
  VulkanGraphicsPipeline(std::shared_ptr<VulkanLogicalDevice> device, const vk::Extent2D &extent,
                         std::shared_ptr<VulkanRenderPass> render_pass);
  bool Init();
  const vk::UniquePipeline &graphics_pipeline() const;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(VulkanGraphicsPipeline);

  bool initialized_;
  std::shared_ptr<VulkanLogicalDevice> device_;
  const vk::Extent2D extent_;
  std::shared_ptr<VulkanRenderPass> render_pass_;

  vk::UniquePipelineLayout pipeline_layout_;
  vk::UniquePipeline graphics_pipeline_;
};

#endif  // GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_GRAPHICS_PIPELINE_H_
