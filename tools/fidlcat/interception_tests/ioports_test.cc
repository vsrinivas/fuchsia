// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"

namespace fidlcat {

// zx_ioports_request tests.

std::unique_ptr<SystemCallTest> ZxIoportsRequest(int64_t result, std::string_view result_name,
                                                 zx_handle_t resource, uint16_t io_addr,
                                                 uint32_t len) {
  auto value = std::make_unique<SystemCallTest>("zx_ioports_request", result, result_name);
  value->AddInput(resource);
  value->AddInput(io_addr);
  value->AddInput(len);
  return value;
}

#define IOPORTS_REQUEST_DISPLAY_TEST_CONTENT(result, expected) \
  PerformDisplayTest("zx_ioports_request@plt",                 \
                     ZxIoportsRequest(result, #result, kHandle, 0x1230, 16), expected);

#define IOPORTS_REQUEST_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {               \
    IOPORTS_REQUEST_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                         \
  TEST_F(InterceptionWorkflowTestArm, name) {               \
    IOPORTS_REQUEST_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

IOPORTS_REQUEST_DISPLAY_TEST(ZxIoportsRequest, ZX_OK,
                             "\n"
                             "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                             "zx_ioports_request("
                             "resource:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                             "io_addr:\x1B[32muint16\x1B[0m: \x1B[34m1230\x1B[0m, "
                             "len:\x1B[32muint32\x1B[0m: \x1B[34m16\x1B[0m)\n"
                             "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_ioports_release tests.

std::unique_ptr<SystemCallTest> ZxIoportsRelease(int64_t result, std::string_view result_name,
                                                 zx_handle_t resource, uint16_t io_addr,
                                                 uint32_t len) {
  auto value = std::make_unique<SystemCallTest>("zx_ioports_release", result, result_name);
  value->AddInput(resource);
  value->AddInput(io_addr);
  value->AddInput(len);
  return value;
}

#define IOPORTS_RELEASE_DISPLAY_TEST_CONTENT(result, expected) \
  PerformDisplayTest("zx_ioports_release@plt",                 \
                     ZxIoportsRelease(result, #result, kHandle, 0x1230, 16), expected);

#define IOPORTS_RELEASE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {               \
    IOPORTS_RELEASE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                         \
  TEST_F(InterceptionWorkflowTestArm, name) {               \
    IOPORTS_RELEASE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

IOPORTS_RELEASE_DISPLAY_TEST(ZxIoportsRelease, ZX_OK,
                             "\n"
                             "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                             "zx_ioports_release("
                             "resource:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                             "io_addr:\x1B[32muint16\x1B[0m: \x1B[34m1230\x1B[0m, "
                             "len:\x1B[32muint32\x1B[0m: \x1B[34m16\x1B[0m)\n"
                             "  -> \x1B[32mZX_OK\x1B[0m\n");

}  // namespace fidlcat
