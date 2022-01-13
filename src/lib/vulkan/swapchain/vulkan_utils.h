// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_VULKAN_SWAPCHAIN_VULKAN_UTILS_H_
#define SRC_LIB_VULKAN_SWAPCHAIN_VULKAN_UTILS_H_

#include <vulkan/vulkan.h>

namespace image_pipe_swapchain {

// Return true if |format| is one of the formats that can be treated as a YUV format.
// Currently these include:
//   - VK_FORMAT_G8B8G8R8_422_UNORM
//   - VK_FORMAT_G8_B8R8_2PLANE_420_UNORM
//   - VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM
// TODO(fxbug.dev/24595): use of these formats is not enough to assume NV12,
// but they're currently the only formats we support at the sampler level.
bool IsYuvFormat(VkFormat format);

// Given a |usage| field from a |VkImageCreateInfo|, return the
// |VkFormatFeatureFlags| required for memory used to store the image.
VkFormatFeatureFlags GetFormatFeatureFlagsFromUsage(VkImageUsageFlags usage);

}  // namespace image_pipe_swapchain

#endif  // SRC_LIB_VULKAN_SWAPCHAIN_VULKAN_UTILS_H_
