// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"

namespace fidlcat {

// zx_event_create tests.

std::unique_ptr<SystemCallTest> ZxEventCreate(int64_t result, std::string_view result_name,
                                              uint32_t options, zx_handle_t* out) {
  auto value = std::make_unique<SystemCallTest>("zx_event_create", result, result_name);
  value->AddInput(options);
  value->AddInput(reinterpret_cast<uint64_t>(out));
  return value;
}

#define EVENT_CREATE_DISPLAY_TEST_CONTENT(result, expected) \
  zx_handle_t out = kHandleOut;                             \
  PerformDisplayTest("$plt(zx_event_create)", ZxEventCreate(result, #result, 0, &out), expected);

#define EVENT_CREATE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {            \
    EVENT_CREATE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                      \
  TEST_F(InterceptionWorkflowTestArm, name) { EVENT_CREATE_DISPLAY_TEST_CONTENT(errno, expected); }

EVENT_CREATE_DISPLAY_TEST(
    ZxEventCreate, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_event_create(options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (out:\x1B[32mhandle\x1B[0m: \x1B[31mbde90caf\x1B[0m)\n");

// zx_eventpair_create tests.

std::unique_ptr<SystemCallTest> ZxEventPairCreate(int64_t result, std::string_view result_name,
                                                  uint32_t options, zx_handle_t* out0,
                                                  zx_handle_t* out1) {
  auto value = std::make_unique<SystemCallTest>("zx_eventpair_create", result, result_name);
  value->AddInput(options);
  value->AddInput(reinterpret_cast<uint64_t>(out0));
  value->AddInput(reinterpret_cast<uint64_t>(out1));
  return value;
}

#define EVENTPAIR_CREATE_DISPLAY_TEST_CONTENT(result, expected) \
  zx_handle_t out0 = kHandleOut;                                \
  zx_handle_t out1 = kHandleOut2;                               \
  PerformDisplayTest("$plt(zx_eventpair_create)",               \
                     ZxEventPairCreate(result, #result, 0, &out0, &out1), expected);

#define EVENTPAIR_CREATE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                \
    EVENTPAIR_CREATE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                          \
  TEST_F(InterceptionWorkflowTestArm, name) {                \
    EVENTPAIR_CREATE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

EVENTPAIR_CREATE_DISPLAY_TEST(
    ZxEventPairCreate, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_eventpair_create(options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m ("
    "out0:\x1B[32mhandle\x1B[0m: \x1B[31mbde90caf\x1B[0m, "
    "out1:\x1B[32mhandle\x1B[0m: \x1B[31mbde90222\x1B[0m)\n");

}  // namespace fidlcat
