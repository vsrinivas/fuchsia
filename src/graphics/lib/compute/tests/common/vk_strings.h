// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_STRINGS_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_STRINGS_H_

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

// Helper functions to convert Vulkan-specific typed values to human-readable
// strings for debugging.

// TECHNICAL NOTE: Client should assume that the content of the pointers
// returned by these functions has a very short lifecycle. I.e. do not hold
// these pointers for too long (e.g. just print them to stdout as soon as
// possible).

// This is done by allocating temporary string buffers from a global bounded
// buffer that will get reused / overwritten periodically. This design keeps
// the API dead simple (no string copies by default), allowing writing simple
// debugging code.

extern const char *
vk_device_size_to_string(VkDeviceSize size);

extern const char *
vk_queue_family_index_to_string(uint32_t queue_family_index);

extern const char *
vk_memory_heap_to_string(const VkMemoryHeap * memory_heap);

extern const char *
vk_memory_type_to_string(const VkMemoryType * memory_type);

extern const char *
vk_present_mode_khr_to_string(VkPresentModeKHR mode);

extern const char *
vk_format_to_string(VkFormat arg);

extern const char *
vk_colorspace_khr_to_string(VkColorSpaceKHR arg);

extern const char *
vk_surface_format_khr_to_string(VkSurfaceFormatKHR format);

extern const char *
vk_format_feature_flags_to_string(VkFormatFeatureFlags flags);

extern const char *
vk_image_usage_flags_to_string(VkImageUsageFlags flags);

extern const char *
vk_buffer_usage_flags_to_string(VkBufferUsageFlags flags);

// Convert physical device type to a string.
extern const char *
vk_physical_device_type_to_string(VkPhysicalDeviceType device_type);

#ifdef __cplusplus
}
#endif

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_STRINGS_H_
