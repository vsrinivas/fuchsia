// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/vulkan_swapchain.h"

namespace escher {

VulkanSwapchain::VulkanSwapchain(vk::SwapchainKHR swapchain_in,
                                 std::vector<ImagePtr> images_in,
                                 uint32_t width_in, uint32_t height_in,
                                 vk::Format format_in,
                                 vk::ColorSpaceKHR color_space_in)
    : swapchain(swapchain_in),
      images(std::move(images_in)),
      width(width_in),
      height(height_in),
      format(format_in),
      color_space(color_space_in) {
  for (auto& im : images) {
    im->set_swapchain_layout(vk::ImageLayout::ePresentSrcKHR);
  }
}

VulkanSwapchain::VulkanSwapchain() : width(UINT32_MAX), height(UINT32_MAX) {}

}  // namespace escher
