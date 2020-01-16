// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_VULKAN_IMAGE_H_
#define SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_VULKAN_IMAGE_H_

#include "src/lib/fxl/macros.h"
#include "vulkan_logical_device.h"

#include <vulkan/vulkan.hpp>

class VulkanImageView {
 public:
  VulkanImageView(std::shared_ptr<VulkanLogicalDevice> device,
      const vk::Extent2D &extent = {1024, 768});
  VulkanImageView() = delete;

  bool Init();

  const vk::Extent2D &extent() const { return extent_; }
  const vk::Format &format() const { return format_; }
  const vk::UniqueImage &image() const { return image_; }
  const vk::UniqueImageView &view() const { return view_; }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(VulkanImageView);

  bool initialized_;
  std::shared_ptr<VulkanLogicalDevice> device_;
  vk::UniqueImage image_;
  vk::Extent2D extent_;
  vk::Format format_;
  vk::UniqueImageView view_;
};

#endif  // SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_VULKAN_IMAGE_H_
