// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <vulkan/vulkan.hpp>

#include "escher/forward_declarations.h"
#include "escher/renderer/image.h"

namespace escher {

struct VulkanSwapchain {
  vk::SwapchainKHR swapchain;
  std::vector<ImagePtr> images;
  uint32_t width;
  uint32_t height;
  vk::Format format;
  vk::ColorSpaceKHR color_space;

  VulkanSwapchain(vk::SwapchainKHR swapchain_in,
                  std::vector<ImagePtr> images_in,
                  uint32_t width_in,
                  uint32_t height_in,
                  vk::Format format_in,
                  vk::ColorSpaceKHR color_space_in)
      : swapchain(swapchain_in),
        images(std::move(images_in)),
        width(width_in),
        height(height_in),
        format(format_in),
        color_space(color_space_in) {}

  VulkanSwapchain() : width(UINT32_MAX), height(UINT32_MAX) {}
};

}  // namespace escher
