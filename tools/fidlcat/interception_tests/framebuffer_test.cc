// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"

namespace fidlcat {

// zx_framebuffer_get_info tests.

std::unique_ptr<SystemCallTest> ZxFramebufferGetInfo(int64_t result, std::string_view result_name,
                                                     zx_handle_t resource, uint32_t* format,
                                                     uint32_t* width, uint32_t* height,
                                                     uint32_t* stride) {
  auto value = std::make_unique<SystemCallTest>("zx_framebuffer_get_info", result, result_name);
  value->AddInput(resource);
  value->AddInput(reinterpret_cast<uint64_t>(format));
  value->AddInput(reinterpret_cast<uint64_t>(width));
  value->AddInput(reinterpret_cast<uint64_t>(height));
  value->AddInput(reinterpret_cast<uint64_t>(stride));
  return value;
}

#define FRAMEBUFFER_GET_INFO_DISPLAY_TEST_CONTENT(result, expected)                      \
  uint32_t format = 1;                                                                   \
  uint32_t width = 1080;                                                                 \
  uint32_t height = 64;                                                                  \
  uint32_t stride = 0;                                                                   \
  PerformDisplayTest(                                                                    \
      "$plt(zx_framebuffer_get_info)",                                                   \
      ZxFramebufferGetInfo(result, #result, kHandle, &format, &width, &height, &stride), \
      expected);

#define FRAMEBUFFER_GET_INFO_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                    \
    FRAMEBUFFER_GET_INFO_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                              \
  TEST_F(InterceptionWorkflowTestArm, name) {                    \
    FRAMEBUFFER_GET_INFO_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

FRAMEBUFFER_GET_INFO_DISPLAY_TEST(
    ZxFramebufferGetInfo, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_framebuffer_get_info(resource:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m ("
    "format:\x1B[32muint32\x1B[0m: \x1B[34m1\x1B[0m, "
    "width:\x1B[32muint32\x1B[0m: \x1B[34m1080\x1B[0m, "
    "height:\x1B[32muint32\x1B[0m: \x1B[34m64\x1B[0m, "
    "stride:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n");

// zx_framebuffer_set_range tests.

std::unique_ptr<SystemCallTest> ZxFramebufferSetRange(int64_t result, std::string_view result_name,
                                                      zx_handle_t resource, zx_handle_t vmo,
                                                      uint32_t len, uint32_t format, uint32_t width,
                                                      uint32_t height, uint32_t stride) {
  auto value = std::make_unique<SystemCallTest>("zx_framebuffer_set_range", result, result_name);
  value->AddInput(resource);
  value->AddInput(vmo);
  value->AddInput(len);
  value->AddInput(format);
  value->AddInput(width);
  value->AddInput(height);
  value->AddInput(stride);
  return value;
}

#define FRAMEBUFFER_SET_RANGE_DISPLAY_TEST_CONTENT(result, expected) \
  PerformDisplayTest(                                                \
      "$plt(zx_framebuffer_set_range)",                              \
      ZxFramebufferSetRange(result, #result, kHandle, kHandle2, 2000, 1, 1080, 64, 0), expected);

#define FRAMEBUFFER_SET_RANGE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                     \
    FRAMEBUFFER_SET_RANGE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                               \
  TEST_F(InterceptionWorkflowTestArm, name) {                     \
    FRAMEBUFFER_SET_RANGE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

FRAMEBUFFER_SET_RANGE_DISPLAY_TEST(ZxFramebufferSetRange, ZX_OK,
                                   "\n"
                                   "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                                   "zx_framebuffer_set_range("
                                   "resource:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                                   "vmo:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1222\x1B[0m, "
                                   "len:\x1B[32muint32\x1B[0m: \x1B[34m2000\x1B[0m, "
                                   "format:\x1B[32muint32\x1B[0m: \x1B[34m1\x1B[0m, "
                                   "width:\x1B[32muint32\x1B[0m: \x1B[34m1080\x1B[0m, "
                                   "height:\x1B[32muint32\x1B[0m: \x1B[34m64\x1B[0m, "
                                   "stride:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
                                   "  -> \x1B[32mZX_OK\x1B[0m\n");

}  // namespace fidlcat
