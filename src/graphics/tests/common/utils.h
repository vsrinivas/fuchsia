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

#endif  // SRC_GRAPHICS_TESTS_COMMON_UTILS_H_
