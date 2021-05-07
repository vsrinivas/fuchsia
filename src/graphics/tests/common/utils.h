// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_GRAPHICS_TESTS_COMMON_UTILS_H_
#define SRC_GRAPHICS_TESTS_COMMON_UTILS_H_

#include <stdio.h>

#include <string>

#include "vulkan/vulkan_core.h"

#include "vulkan/vulkan.hpp"

#define RTN_MSG(err, ...)                          \
  {                                                \
    fprintf(stderr, "%s:%d ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__);                  \
    fflush(stderr);                                \
    return err;                                    \
  }

// Log and return based on VkResult |r|.
#define RTN_IF_VK_ERR(err, r, ...)                                      \
  if (r != VK_SUCCESS) {                                                \
    fprintf(stderr, "%s:%d:\n\t(vk::Result::e%s) ", __FILE__, __LINE__, \
            vk::to_string(vk::Result(r)).c_str());                      \
    fprintf(stderr, __VA_ARGS__);                                       \
    fprintf(stderr, "\n");                                              \
    fflush(stderr);                                                     \
    return err;                                                         \
  }

// Log and return based on vk::Result |r|.
#define RTN_IF_VKH_ERR(err, r, ...)                                                                \
  if (r != vk::Result::eSuccess) {                                                                 \
    fprintf(stderr, "%s:%d:\n\t(vk::Result::e%s) ", __FILE__, __LINE__, vk::to_string(r).c_str()); \
    fprintf(stderr, __VA_ARGS__);                                                                  \
    fprintf(stderr, "\n");                                                                         \
    fflush(stderr);                                                                                \
    return err;                                                                                    \
  }

//
// DebugUtilsTestCallback will fail an EXPECT_TRUE() test if validation errors should
// not be ignored and the message severity is of type ::eError.  It directs errors to stderr
// and other severities to stdout.
//
// See test_vkcontext.cc for an example of how to send user data into the callback.
//
VKAPI_ATTR VkBool32 VKAPI_CALL
DebugUtilsTestCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                       VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                       const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, void *pUserData);

// TODO(fxbug.dev/73025): remove this condition block when it's time.
#if VK_HEADER_VERSION < 174
constexpr VkExternalMemoryHandleTypeFlagBits VK_EXTERNAL_MEMORY_HANDLE_TYPE_ZIRCON_VMO_BIT_FUCHSIA =
    static_cast<VkExternalMemoryHandleTypeFlagBits>(0x00000800);
constexpr VkExternalSemaphoreHandleTypeFlagBits
    VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_ZIRCON_EVENT_BIT_FUCHSIA =
        static_cast<VkExternalSemaphoreHandleTypeFlagBits>(0x00000080);
constexpr uint32_t VK_STRUCTURE_TYPE_IMPORT_MEMORY_ZIRCON_HANDLE_INFO_FUCHSIA = 1000364000;
constexpr uint32_t VK_STRUCTURE_TYPE_MEMORY_ZIRCON_HANDLE_PROPERTIES_FUCHSIA = 1000364001;
constexpr uint32_t VK_STRUCTURE_TYPE_MEMORY_GET_ZIRCON_HANDLE_INFO_FUCHSIA = 1000364002;
constexpr uint32_t VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_ZIRCON_HANDLE_INFO_FUCHSIA = 1000365000;
constexpr uint32_t VK_STRUCTURE_TYPE_SEMAPHORE_GET_ZIRCON_HANDLE_INFO_FUCHSIA = 1000365001;
#else
constexpr VkExternalMemoryHandleTypeFlagBits
    VK_EXTERNAL_MEMORY_HANDLE_TYPE_TEMP_ZIRCON_VMO_BIT_FUCHSIA =
        static_cast<VkExternalMemoryHandleTypeFlagBits>(0x00100000);
constexpr VkExternalSemaphoreHandleTypeFlagBits
    VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_TEMP_ZIRCON_EVENT_BIT_FUCHSIA =
        static_cast<VkExternalSemaphoreHandleTypeFlagBits>(0x00100000);
constexpr uint32_t VK_STRUCTURE_TYPE_TEMP_IMPORT_MEMORY_ZIRCON_HANDLE_INFO_FUCHSIA = 1001005000;
constexpr uint32_t VK_STRUCTURE_TYPE_TEMP_MEMORY_ZIRCON_HANDLE_PROPERTIES_FUCHSIA = 1001005001;
constexpr uint32_t VK_STRUCTURE_TYPE_TEMP_MEMORY_GET_ZIRCON_HANDLE_INFO_FUCHSIA = 1001005002;
constexpr uint32_t VK_STRUCTURE_TYPE_TEMP_IMPORT_SEMAPHORE_ZIRCON_HANDLE_INFO_FUCHSIA = 1001006000;
constexpr uint32_t VK_STRUCTURE_TYPE_TEMP_SEMAPHORE_GET_ZIRCON_HANDLE_INFO_FUCHSIA = 1001006001;
#endif

#endif  // SRC_GRAPHICS_TESTS_COMMON_UTILS_H_
