// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_UTILS_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_UTILS_H_

#include <stdbool.h>
#include <vulkan/vulkan.h>

#include "utils.h"  // For FUNC_ATTRIBUTE_NORETURN

#ifdef __cplusplus
extern "C" {
#endif

// Technical note:
//
// The Vulkan specification reserves the following for its own use:
//
// - Function names with a "vk" prefix, followed by a capital letter.
// - Macro names with a "VK_" prefix.

// Check that |result| is VK_SUCCESS, if not, immediately panic.
//
// Usage example:
//   VkResult result = vkCreateDevice(...);
//   VK_CHECK(result);
//
#define VK_CHECK(result)                                                                           \
  do                                                                                               \
    {                                                                                              \
      VkResult vk_result_ = (result);                                                              \
      if (vk_result_ != VK_SUCCESS)                                                                \
        vk_panic_(vk_result_, __FILE__, __LINE__, NULL);                                           \
    }                                                                                              \
  while (0)

// Check that |result| is VK_SUCCESS. If not, panic immediately with a formatted message.
//
// Usage example:
//   VkResult result = vkCreateDevice(...);
//   VK_CHECK_MSG(result, "Could not create device for instance %p", instance);
//
#define VK_CHECK_MSG(result, ...)                                                                  \
  do                                                                                               \
    {                                                                                              \
      VkResult vk_result_ = (result);                                                              \
      if (vk_result_ != VK_SUCCESS)                                                                \
        vk_panic_(vk_result_, __FILE__, __LINE__, __VA_ARGS__);                                    \
    }                                                                                              \
  while (0)

// Convenience macro used to embed a Vulkan function call and panic if it fails.
// Usage example:
//    vk(CreateInstance(....));
//
// Equivalent to:
//    VK_CHECK(vkCreateInstance(...));
//
#define vk(...)                                                                                    \
  do                                                                                               \
    {                                                                                              \
      VkResult vk_result_ = vk##__VA_ARGS__;                                                       \
      if (vk_result_ != VK_SUCCESS)                                                                \
        vk_panic_(vk_result_, __FILE__, __LINE__, NULL);                                           \
    }                                                                                              \
  while (0)

// Internal panic function to call in case of a failed Vulkan call with |result|.
// See vk_check() below for usage.
extern void
vk_panic_(VkResult result, const char * file, int line, const char * fmt, ...)
  FUNC_ATTRIBUTE_NORETURN;

// Convert VkResult value to a string.
extern const char *
vk_result_to_string(VkResult result);

// Helper macro to define a local variable pointing a global Vulkan entry point,
// i.e. those that are available before VkInstance creation.
//
// Usage example:
//    GET_VULKAN_GLOBAL_PROC_ADDR(vkEnumerateInstanceLayerProperties);
//    VkResult result = vkEnumerateInstanceLayerProperties(...);
//
#define GET_VULKAN_GLOBAL_PROC_ADDR(name)                                                          \
  PFN_##name name = (PFN_##name)vkGetInstanceProcAddr(NULL, #name)

// Helper macro to define a local variable pointing an instance-specific
// Vulkan entry point, i.e. those available just after VkInstance creation.
// WARNING: This assumes the VkInstance handle to use is named 'instance'.
//
// Usage example:
//    VkInstance instance = ...;
//    GET_VULKAN_INSTANCE_PROC_ADDR(vkCreateDevice);
//    VkResult result = vkCreateDevice(...);
//
#define GET_VULKAN_INSTANCE_PROC_ADDR(name)                                                        \
  PFN_##name name = (PFN_##name)vkGetInstanceProcAddr(instance, #name)

// Helper macro to define a local variable pointing a device-specific
// Vulkan entry point, i.e. those available just after VkInstance creation.
// WARNING: This assumes the VkDevice handle to use is named 'device'.
//
// Usage example:
//    VkDevice device = ...;
//    GET_VULKAN_DEVICE_PROC_ADDR(vkCreateBuffer);
//    VkResult result = vkCreateBuffer(...);
//
#define GET_VULKAN_DEVICE_PROC_ADDR(name)                                                          \
  PFN_##name name = (PFN_##name)vkGetDeviceProcAddr(device, #name)

// Print the content of a given VkInstanceCreateInfo to stderr. Useful for debugging.
extern void
vk_instance_create_info_print(const VkInstanceCreateInfo * info);

// Print the content of a given VkDeviceCreateInfo to stderr. Useful for debugging.
extern void
vk_device_create_info_print(const VkDeviceCreateInfo * info);

// Check that all bits in |image_usage| are supported by |format_features|
// when that makes sense (i.e. not all image usage bits have a corresponding
// format feature bit).
extern bool
vk_check_image_usage_vs_format_features(VkImageUsageFlags    image_usage,
                                        VkFormatFeatureFlags format_features);

#ifdef __cplusplus
}
#endif

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_UTILS_H_
