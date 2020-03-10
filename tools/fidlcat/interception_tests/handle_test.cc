// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"

namespace fidlcat {

// zx_handle_close tests.

std::unique_ptr<SystemCallTest> ZxHandleClose(int64_t result, std::string_view result_name,
                                              zx_handle_t handle) {
  auto value = std::make_unique<SystemCallTest>("zx_handle_close", result, result_name);
  value->AddInput(handle);
  return value;
}

#define HANDLE_CLOSE_DISPLAY_TEST_CONTENT(result, expected) \
  PerformDisplayTest("$plt(zx_handle_close)", ZxHandleClose(result, #result, kHandle), expected);

#define HANDLE_CLOSE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {            \
    HANDLE_CLOSE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                      \
  TEST_F(InterceptionWorkflowTestArm, name) { HANDLE_CLOSE_DISPLAY_TEST_CONTENT(errno, expected); }

HANDLE_CLOSE_DISPLAY_TEST(ZxHandleClose, ZX_OK,
                          "\n"
                          "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                          "zx_handle_close("
                          "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m)\n"
                          "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_handle_close_many tests.

std::unique_ptr<SystemCallTest> ZxHandleCloseMany(int64_t result, std::string_view result_name,
                                                  const zx_handle_t* handles, size_t num_handles) {
  auto value = std::make_unique<SystemCallTest>("zx_handle_close_many", result, result_name);
  value->AddInput(reinterpret_cast<uint64_t>(handles));
  value->AddInput(num_handles);
  return value;
}

#define HANDLE_CLOSE_MANY_DISPLAY_TEST_CONTENT(result, expected)                         \
  std::vector<zx_handle_t> handles = {kHandle, kHandle2, kHandle3};                      \
  PerformDisplayTest("$plt(zx_handle_close_many)",                                       \
                     ZxHandleCloseMany(result, #result, handles.data(), handles.size()), \
                     expected);

#define HANDLE_CLOSE_MANY_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                 \
    HANDLE_CLOSE_MANY_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                           \
  TEST_F(InterceptionWorkflowTestArm, name) {                 \
    HANDLE_CLOSE_MANY_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

HANDLE_CLOSE_MANY_DISPLAY_TEST(
    ZxHandleCloseMany, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_handle_close_many()\n"
    "    handles:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, \x1B[31mcefa1222\x1B[0m, "
    "\x1B[31mcefa1333\x1B[0m\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_handle_duplicate tests.

std::unique_ptr<SystemCallTest> ZxHandleDuplicate(int64_t result, std::string_view result_name,
                                                  zx_handle_t handle, zx_rights_t rights,
                                                  zx_handle_t* out) {
  auto value = std::make_unique<SystemCallTest>("zx_handle_duplicate", result, result_name);
  value->AddInput(handle);
  value->AddInput(rights);
  value->AddInput(reinterpret_cast<uint64_t>(out));
  return value;
}

#define HANDLE_DUPLICATE_DISPLAY_TEST_CONTENT(result, expected)                               \
  zx_handle_t out = kHandleOut;                                                               \
  PerformDisplayTest("$plt(zx_handle_duplicate)",                                             \
                     ZxHandleDuplicate(result, #result, kHandle, ZX_RIGHT_SAME_RIGHTS, &out), \
                     expected);

#define HANDLE_DUPLICATE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                \
    HANDLE_DUPLICATE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                          \
  TEST_F(InterceptionWorkflowTestArm, name) {                \
    HANDLE_DUPLICATE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

HANDLE_DUPLICATE_DISPLAY_TEST(
    ZxHandleDuplicate, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_handle_duplicate("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "rights:\x1B[32mzx_rights_t\x1B[0m: \x1B[34mZX_RIGHT_SAME_RIGHTS\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (out:\x1B[32mhandle\x1B[0m: \x1B[31mbde90caf\x1B[0m)\n");

// zx_handle_replace tests.

std::unique_ptr<SystemCallTest> ZxHandleReplace(int64_t result, std::string_view result_name,
                                                zx_handle_t handle, zx_rights_t rights,
                                                zx_handle_t* out) {
  auto value = std::make_unique<SystemCallTest>("zx_handle_replace", result, result_name);
  value->AddInput(handle);
  value->AddInput(rights);
  value->AddInput(reinterpret_cast<uint64_t>(out));
  return value;
}

#define HANDLE_REPLACE_DISPLAY_TEST_CONTENT(result, expected)                               \
  zx_handle_t out = kHandleOut;                                                             \
  PerformDisplayTest("$plt(zx_handle_replace)",                                             \
                     ZxHandleReplace(result, #result, kHandle, ZX_RIGHT_SAME_RIGHTS, &out), \
                     expected);

#define HANDLE_REPLACE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {              \
    HANDLE_REPLACE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                        \
  TEST_F(InterceptionWorkflowTestArm, name) {              \
    HANDLE_REPLACE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

HANDLE_REPLACE_DISPLAY_TEST(
    ZxHandleReplace, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_handle_replace("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "rights:\x1B[32mzx_rights_t\x1B[0m: \x1B[34mZX_RIGHT_SAME_RIGHTS\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (out:\x1B[32mhandle\x1B[0m: \x1B[31mbde90caf\x1B[0m)\n");

}  // namespace fidlcat
