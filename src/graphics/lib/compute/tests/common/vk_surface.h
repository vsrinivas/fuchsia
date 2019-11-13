// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_SURFACE_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_SURFACE_H_

#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan_core.h>

#ifdef __cplusplus
extern "C" {
#endif

// A struct modelling the surface formats that a given physical device
// supports for presentation, given a VkSurfaceKHR instance.
//
typedef struct
{
  VkPhysicalDevice         physical_device;
  VkSurfaceCapabilitiesKHR capabilities;

  uint32_t           present_modes_count;
  VkPresentModeKHR * present_modes;

  uint32_t             formats_count;
  VkSurfaceFormatKHR * formats;

} vk_device_surface_info_t;

// Initialize a new vk_device_surface_info_t instance.
extern void
vk_device_surface_info_init(vk_device_surface_info_t * info,
                            VkPhysicalDevice           physical_device,
                            VkSurfaceKHR               surface,
                            VkInstance                 instance);

// Destroy a given vk_device_surface_info_t instance.
extern void
vk_device_surface_info_destroy(vk_device_surface_info_t * info);

// Probe all surface formats to find the one that best matches |wanted_image_usage|
// and |wanted_format|. Returns VK_FORMAT_UNDEFINED if none is found, or a valid
// and compatible VkFormat value otherwise.
//
// If |wanted_image_usage| is not 0, all bits in it should be supported by the
// result format.
//
// If |wanted_format| is not VK_FORMAT_UNDEFINED, then it will be the value returned
// by this function in case of success.
extern VkFormat
vk_device_surface_info_find_presentation_format(vk_device_surface_info_t * info,
                                                VkImageUsageFlags          wanted_image_usage,
                                                VkFormat                   wanted_format);

// Print the content of |info| to stdout for debugging.
extern void
vk_device_surface_info_print(const vk_device_surface_info_t * info);

#ifdef __cplusplus
}
#endif

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_SURFACE_H_
