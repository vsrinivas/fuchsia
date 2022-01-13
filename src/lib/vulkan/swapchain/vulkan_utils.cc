// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/vulkan/swapchain/vulkan_utils.h"

namespace image_pipe_swapchain {

bool IsYuvFormat(VkFormat format) {
  switch (format) {
    case VK_FORMAT_G8B8G8R8_422_UNORM:
    case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
    case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
      return true;
    default:
      return false;
  }
}

// TODO(fxbug.dev/91193): Support more usage cases and feature flags.
VkFormatFeatureFlags GetFormatFeatureFlagsFromUsage(VkImageUsageFlags usage) {
  VkFormatFeatureFlags result = {};
  if (usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) {
    result |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
  }
  if (usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
    result |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
  }
  if (usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
    result |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
  }
  if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
    result |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
  }
  if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
    result |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
  }
  if (usage & VK_IMAGE_USAGE_STORAGE_BIT) {
    result |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
  }
  return result;
}

}  // namespace image_pipe_swapchain
