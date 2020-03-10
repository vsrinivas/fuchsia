// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"

namespace fidlcat {

// zx_fifo_create tests.

std::unique_ptr<SystemCallTest> ZxFifoCreate(int64_t result, std::string_view result_name,
                                             size_t elem_count, size_t elem_size, uint32_t options,
                                             zx_handle_t* out0, zx_handle_t* out1) {
  auto value = std::make_unique<SystemCallTest>("zx_fifo_create", result, result_name);
  value->AddInput(elem_count);
  value->AddInput(elem_size);
  value->AddInput(options);
  value->AddInput(reinterpret_cast<uint64_t>(out0));
  value->AddInput(reinterpret_cast<uint64_t>(out1));
  return value;
}

#define FIFO_CREATE_DISPLAY_TEST_CONTENT(result, expected)                                         \
  zx_handle_t out0 = kHandleOut;                                                                   \
  zx_handle_t out1 = kHandleOut2;                                                                  \
  PerformDisplayTest("$plt(zx_fifo_create)", ZxFifoCreate(result, #result, 4, 3, 0, &out0, &out1), \
                     expected);

#define FIFO_CREATE_DISPLAY_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { FIFO_CREATE_DISPLAY_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { FIFO_CREATE_DISPLAY_TEST_CONTENT(errno, expected); }

FIFO_CREATE_DISPLAY_TEST(ZxFifoCreate, ZX_OK,
                         "\n"
                         "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                         "zx_fifo_create("
                         "elem_count:\x1B[32msize_t\x1B[0m: \x1B[34m4\x1B[0m, "
                         "elem_size:\x1B[32msize_t\x1B[0m: \x1B[34m3\x1B[0m, "
                         "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
                         "  -> \x1B[32mZX_OK\x1B[0m ("
                         "out0:\x1B[32mhandle\x1B[0m: \x1B[31mbde90caf\x1B[0m, "
                         "out1:\x1B[32mhandle\x1B[0m: \x1B[31mbde90222\x1B[0m)\n");

// zx_fifo_read tests.

std::unique_ptr<SystemCallTest> ZxFifoRead(int64_t result, std::string_view result_name,
                                           zx_handle_t handle, size_t elem_size, void* data,
                                           size_t count, size_t* actual_count) {
  auto value = std::make_unique<SystemCallTest>("zx_fifo_read", result, result_name);
  value->AddInput(handle);
  value->AddInput(elem_size);
  value->AddInput(reinterpret_cast<uint64_t>(data));
  value->AddInput(count);
  value->AddInput(reinterpret_cast<uint64_t>(actual_count));
  return value;
}

#define FIFO_READ_DISPLAY_TEST_CONTENT(result, expected)                                 \
  constexpr size_t kElemSize = 4;                                                        \
  std::vector<uint8_t> buffer;                                                           \
  for (int i = 0; i < 20; ++i) {                                                         \
    buffer.emplace_back(i);                                                              \
  }                                                                                      \
  size_t actual_count = buffer.size() / kElemSize;                                       \
  PerformDisplayTest(                                                                    \
      "$plt(zx_fifo_read)",                                                              \
      ZxFifoRead(result, #result, kHandle, kElemSize, buffer.data(), 10, &actual_count), \
      expected);

#define FIFO_READ_DISPLAY_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { FIFO_READ_DISPLAY_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { FIFO_READ_DISPLAY_TEST_CONTENT(errno, expected); }

FIFO_READ_DISPLAY_TEST(
    ZxFifoRead, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_fifo_read("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "elem_size:\x1B[32msize_t\x1B[0m: \x1B[34m4\x1B[0m, "
    "count:\x1B[32msize_t\x1B[0m: \x1B[34m10\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (actual:\x1B[32msize_t\x1B[0m: \x1B[34m5\x1B[0m/\x1B[34m10\x1B[0m)\n"
    "      data:\x1B[32muint8\x1B[0m: "
    "\x1B[34m00\x1B[0m, \x1B[34m01\x1B[0m, \x1B[34m02\x1B[0m, \x1B[34m03\x1B[0m, "
    "\x1B[34m04\x1B[0m, \x1B[34m05\x1B[0m, \x1B[34m06\x1B[0m, \x1B[34m07\x1B[0m, "
    "\x1B[34m08\x1B[0m, \x1B[34m09\x1B[0m, \x1B[34m0a\x1B[0m, \x1B[34m0b\x1B[0m, "
    "\x1B[34m0c\x1B[0m, \x1B[34m0d\x1B[0m, \x1B[34m0e\x1B[0m, \x1B[34m0f\x1B[0m, "
    "\x1B[34m10\x1B[0m, \x1B[34m11\x1B[0m, \x1B[34m12\x1B[0m, \x1B[34m13\x1B[0m\n");

// zx_fifo_write tests.

std::unique_ptr<SystemCallTest> ZxFifoWrite(int64_t result, std::string_view result_name,
                                            zx_handle_t handle, size_t elem_size, const void* data,
                                            size_t count, size_t* actual_count) {
  auto value = std::make_unique<SystemCallTest>("zx_fifo_write", result, result_name);
  value->AddInput(handle);
  value->AddInput(elem_size);
  value->AddInput(reinterpret_cast<uint64_t>(data));
  value->AddInput(count);
  value->AddInput(reinterpret_cast<uint64_t>(actual_count));
  return value;
}

#define FIFO_WRITE_DISPLAY_TEST_CONTENT(result, expected)                            \
  constexpr size_t kElemSize = 4;                                                    \
  std::vector<uint8_t> buffer;                                                       \
  for (int i = 0; i < 20; ++i) {                                                     \
    buffer.emplace_back(i);                                                          \
  }                                                                                  \
  size_t actual_count = 2;                                                           \
  PerformDisplayTest("$plt(zx_fifo_write)",                                          \
                     ZxFifoWrite(result, #result, kHandle, kElemSize, buffer.data(), \
                                 buffer.size() / kElemSize, &actual_count),          \
                     expected);

#define FIFO_WRITE_DISPLAY_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { FIFO_WRITE_DISPLAY_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { FIFO_WRITE_DISPLAY_TEST_CONTENT(errno, expected); }

FIFO_WRITE_DISPLAY_TEST(
    ZxFifoWrite, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_fifo_write("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "elem_size:\x1B[32msize_t\x1B[0m: \x1B[34m4\x1B[0m, "
    "count:\x1B[32msize_t\x1B[0m: \x1B[34m5\x1B[0m)\n"
    "    data:\x1B[32muint8\x1B[0m: "
    "\x1B[34m00\x1B[0m, \x1B[34m01\x1B[0m, \x1B[34m02\x1B[0m, \x1B[34m03\x1B[0m, "
    "\x1B[34m04\x1B[0m, \x1B[34m05\x1B[0m, \x1B[34m06\x1B[0m, \x1B[34m07\x1B[0m, "
    "\x1B[34m08\x1B[0m, \x1B[34m09\x1B[0m, \x1B[34m0a\x1B[0m, \x1B[34m0b\x1B[0m, "
    "\x1B[34m0c\x1B[0m, \x1B[34m0d\x1B[0m, \x1B[34m0e\x1B[0m, \x1B[34m0f\x1B[0m, "
    "\x1B[34m10\x1B[0m, \x1B[34m11\x1B[0m, \x1B[34m12\x1B[0m, \x1B[34m13\x1B[0m\n"
    "  -> \x1B[32mZX_OK\x1B[0m ("
    "actual:\x1B[32msize_t\x1B[0m: \x1B[34m2\x1B[0m/\x1B[34m5\x1B[0m)\n");

}  // namespace fidlcat
