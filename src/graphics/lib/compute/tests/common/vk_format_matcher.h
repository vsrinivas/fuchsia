// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_FORMAT_MATCHER_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_FORMAT_MATCHER_H_

#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan_core.h>

#ifdef __cplusplus
extern "C" {
#endif

// Helper struct to probe a list of VkFormat values and find the best match for
// a given VkImageUsageFlags. Usage is:
//
//  1) Call one of the vk_format_matcher_init_xxx() functions.
//  2) For each candidate surface format, call vk_format_matcher_probe()
//  3) Call vk_format_matcher_done() to get matching results.
//
typedef struct
{
  // private
  VkPhysicalDevice     physical_device;
  int                  mode;
  VkImageUsageFlags    image_usage;
  VkFormatFeatureFlags format_features;
  VkFormat             optimal_tiling_format;
  VkFormat             linear_tiling_format;

} vk_format_matcher_t;

// Initialize a vk_format_matcher_t instance to find the best format that
// corresponds to a given |image_usage| for |physical_device|.
extern void
vk_format_matcher_init_for_image_usage(vk_format_matcher_t * matcher,
                                       VkImageUsageFlags     image_usage,
                                       VkPhysicalDevice      physical_device);

// Initialize a vk_format_matcher_t instance to find the best format that
// corresponds to a given set of |format_features| for |physical_device|.
extern void
vk_format_matcher_init_for_format_features(vk_format_matcher_t * matcher,
                                           VkFormatFeatureFlags  format_features,
                                           VkPhysicalDevice      physical_device);

extern void
vk_format_matcher_probe(vk_format_matcher_t * matcher, VkFormat format);

extern bool
vk_format_matcher_done(vk_format_matcher_t * matcher,
                       VkFormat *            p_format,
                       VkImageTiling *       p_tiling);

// Setup a callback that replaces calls to vkGetPhysicalFormatProperties(), used during
// unit testing only. Use a NULL value to restore the default behaviour.
extern void
vk_format_matcher_set_properties_callback_for_testing(
  PFN_vkGetPhysicalDeviceFormatProperties callback);

#ifdef __cplusplus
}
#endif

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_FORMAT_MATCHER_H_
