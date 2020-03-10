// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"

namespace fidlcat {

// zx_socket_create tests.

std::unique_ptr<SystemCallTest> ZxSocketCreate(int64_t result, std::string_view result_name,
                                               uint32_t options, zx_handle_t* out0,
                                               zx_handle_t* out1) {
  auto value = std::make_unique<SystemCallTest>("zx_socket_create", result, result_name);
  value->AddInput(options);
  value->AddInput(reinterpret_cast<uint64_t>(out0));
  value->AddInput(reinterpret_cast<uint64_t>(out1));
  return value;
}

#define SOCKET_CREATE_DISPLAY_TEST_CONTENT(result, expected) \
  zx_handle_t out0 = kHandleOut;                             \
  zx_handle_t out1 = kHandleOut2;                            \
  PerformDisplayTest("$plt(zx_socket_create)",               \
                     ZxSocketCreate(result, #result, ZX_SOCKET_STREAM, &out0, &out1), expected);

#define SOCKET_CREATE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {             \
    SOCKET_CREATE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                       \
  TEST_F(InterceptionWorkflowTestArm, name) { SOCKET_CREATE_DISPLAY_TEST_CONTENT(errno, expected); }

SOCKET_CREATE_DISPLAY_TEST(
    ZxSocketCreate, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_socket_create("
    "options:\x1B[32mzx_socket_create_options_t\x1B[0m: \x1B[34mZX_SOCKET_STREAM\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m ("
    "out0:\x1B[32mhandle\x1B[0m: \x1B[31mbde90caf\x1B[0m, "
    "out1:\x1B[32mhandle\x1B[0m: \x1B[31mbde90222\x1B[0m)\n");

// zx_socket_write tests.

std::unique_ptr<SystemCallTest> ZxSocketWrite(int64_t result, std::string_view result_name,
                                              zx_handle_t handle, uint32_t options,
                                              const uint8_t* buffer, size_t buffer_size,
                                              size_t* actual) {
  auto value = std::make_unique<SystemCallTest>("zx_socket_write", result, result_name);
  value->AddInput(handle);
  value->AddInput(options);
  value->AddInput(reinterpret_cast<uint64_t>(buffer));
  value->AddInput(buffer_size);
  value->AddInput(reinterpret_cast<uint64_t>(actual));
  return value;
}

#define SOCKET_WRITE_DISPLAY_TEST_CONTENT(result, expected)                              \
  std::vector<uint8_t> buffer = {0x10, 0x01, 0x20, 0x02, 0x30, 0x03, 0x40, 0x04};        \
  size_t actual = buffer.size();                                                         \
  PerformDisplayTest(                                                                    \
      "$plt(zx_socket_write)",                                                           \
      ZxSocketWrite(result, #result, kHandle, 0, buffer.data(), buffer.size(), &actual), \
      expected);

#define SOCKET_WRITE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {            \
    SOCKET_WRITE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                      \
  TEST_F(InterceptionWorkflowTestArm, name) { SOCKET_WRITE_DISPLAY_TEST_CONTENT(errno, expected); }

SOCKET_WRITE_DISPLAY_TEST(
    ZxSocketWrite, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_socket_write("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
    "    buffer:\x1B[32muint8\x1B[0m: "
    "\x1B[34m16\x1B[0m, \x1B[34m1\x1B[0m, \x1B[34m32\x1B[0m, \x1B[34m2\x1B[0m, "
    "\x1B[34m48\x1B[0m, \x1B[34m3\x1B[0m, \x1B[34m64\x1B[0m, \x1B[34m4\x1B[0m\n"
    "  -> \x1B[32mZX_OK\x1B[0m ("
    "actual:\x1B[32msize_t\x1B[0m: \x1B[34m8\x1B[0m/\x1B[34m8\x1B[0m)\n");

#define SOCKET_WRITE_STRING_DISPLAY_TEST_CONTENT(result, expected)                       \
  std::vector<uint8_t> buffer = {'h', 'e', 'l', 'l', 'o'};                               \
  size_t actual = buffer.size();                                                         \
  PerformDisplayTest(                                                                    \
      "$plt(zx_socket_write)",                                                           \
      ZxSocketWrite(result, #result, kHandle, 0, buffer.data(), buffer.size(), &actual), \
      expected);

#define SOCKET_WRITE_STRING_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                   \
    SOCKET_WRITE_STRING_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                             \
  TEST_F(InterceptionWorkflowTestArm, name) {                   \
    SOCKET_WRITE_STRING_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

SOCKET_WRITE_STRING_DISPLAY_TEST(
    ZxSocketWriteString, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_socket_write("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
    "    buffer:\x1B[32muint8\x1B[0m: \x1B[31m\"hello\"\x1B[0m\n"
    "  -> \x1B[32mZX_OK\x1B[0m ("
    "actual:\x1B[32msize_t\x1B[0m: \x1B[34m5\x1B[0m/\x1B[34m5\x1B[0m)\n");

// zx_socket_read tests.

std::unique_ptr<SystemCallTest> ZxSocketRead(int64_t result, std::string_view result_name,
                                             zx_handle_t handle, uint32_t options,
                                             const uint8_t* buffer, size_t buffer_size,
                                             size_t* actual) {
  auto value = std::make_unique<SystemCallTest>("zx_socket_read", result, result_name);
  value->AddInput(handle);
  value->AddInput(options);
  value->AddInput(reinterpret_cast<uint64_t>(buffer));
  value->AddInput(buffer_size);
  value->AddInput(reinterpret_cast<uint64_t>(actual));
  return value;
}

#define SOCKET_READ_DISPLAY_TEST_CONTENT(result, options, expected)               \
  std::vector<uint8_t> buffer = {0x10, 0x01, 0x20, 0x02, 0x30, 0x03, 0x40, 0x04}; \
  size_t actual = buffer.size();                                                  \
  PerformDisplayTest(                                                             \
      "$plt(zx_socket_read)",                                                     \
      ZxSocketRead(result, #result, kHandle, options, buffer.data(), 1024, &actual), expected);

#define SOCKET_READ_DISPLAY_TEST(name, errno, options, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                    \
    SOCKET_READ_DISPLAY_TEST_CONTENT(errno, options, expected);  \
  }                                                              \
  TEST_F(InterceptionWorkflowTestArm, name) {                    \
    SOCKET_READ_DISPLAY_TEST_CONTENT(errno, options, expected);  \
  }

SOCKET_READ_DISPLAY_TEST(
    ZxSocketRead, ZX_OK, 0,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_socket_read("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "options:\x1B[32mzx_socket_read_options_t\x1B[0m: \x1B[34m0\x1B[0m, "
    "buffer_size:\x1B[32msize_t\x1B[0m: \x1B[34m1024\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m ("
    "actual:\x1B[32msize_t\x1B[0m: \x1B[34m8\x1B[0m/\x1B[34m1024\x1B[0m)\n"
    "      buffer:\x1B[32muint8\x1B[0m: "
    "\x1B[34m16\x1B[0m, \x1B[34m1\x1B[0m, \x1B[34m32\x1B[0m, \x1B[34m2\x1B[0m, "
    "\x1B[34m48\x1B[0m, \x1B[34m3\x1B[0m, \x1B[34m64\x1B[0m, \x1B[34m4\x1B[0m\n");

SOCKET_READ_DISPLAY_TEST(
    ZxSocketReadPeek, ZX_OK, ZX_SOCKET_PEEK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_socket_read("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "options:\x1B[32mzx_socket_read_options_t\x1B[0m: \x1B[34mZX_SOCKET_PEEK\x1B[0m, "
    "buffer_size:\x1B[32msize_t\x1B[0m: \x1B[34m1024\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m ("
    "actual:\x1B[32msize_t\x1B[0m: \x1B[34m8\x1B[0m/\x1B[34m1024\x1B[0m)\n"
    "      buffer:\x1B[32muint8\x1B[0m: "
    "\x1B[34m16\x1B[0m, \x1B[34m1\x1B[0m, \x1B[34m32\x1B[0m, \x1B[34m2\x1B[0m, "
    "\x1B[34m48\x1B[0m, \x1B[34m3\x1B[0m, \x1B[34m64\x1B[0m, \x1B[34m4\x1B[0m\n");

#define SOCKET_READ_STRING_DISPLAY_TEST_CONTENT(result, expected)                             \
  std::vector<uint8_t> buffer = {'h', 'e', 'l', 'l', 'o'};                                    \
  size_t actual = buffer.size();                                                              \
  PerformDisplayTest("$plt(zx_socket_read)",                                                  \
                     ZxSocketRead(result, #result, kHandle, 0, buffer.data(), 1024, &actual), \
                     expected);

#define SOCKET_READ_STRING_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                  \
    SOCKET_READ_STRING_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                            \
  TEST_F(InterceptionWorkflowTestArm, name) {                  \
    SOCKET_READ_STRING_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

SOCKET_READ_STRING_DISPLAY_TEST(
    ZxSocketReadString, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_socket_read("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "options:\x1B[32mzx_socket_read_options_t\x1B[0m: \x1B[34m0\x1B[0m, "
    "buffer_size:\x1B[32msize_t\x1B[0m: \x1B[34m1024\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m ("
    "actual:\x1B[32msize_t\x1B[0m: \x1B[34m5\x1B[0m/\x1B[34m1024\x1B[0m)\n"
    "      buffer:\x1B[32muint8\x1B[0m: \x1B[31m\"hello\"\x1B[0m\n");

// zx_socket_shutdown tests.

std::unique_ptr<SystemCallTest> ZxSocketShutdown(int64_t result, std::string_view result_name,
                                                 zx_handle_t handle, uint32_t options) {
  auto value = std::make_unique<SystemCallTest>("zx_socket_shutdown", result, result_name);
  value->AddInput(handle);
  value->AddInput(options);
  return value;
}

#define SOCKET_SHUTDOWN_DISPLAY_TEST_CONTENT(result, options, expected) \
  PerformDisplayTest("$plt(zx_socket_shutdown)",                        \
                     ZxSocketShutdown(result, #result, kHandle, options), expected);

#define SOCKET_SHUTDOWN_DISPLAY_TEST(name, errno, options, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                        \
    SOCKET_SHUTDOWN_DISPLAY_TEST_CONTENT(errno, options, expected);  \
  }                                                                  \
  TEST_F(InterceptionWorkflowTestArm, name) {                        \
    SOCKET_SHUTDOWN_DISPLAY_TEST_CONTENT(errno, options, expected);  \
  }

SOCKET_SHUTDOWN_DISPLAY_TEST(
    ZxSocketShutdownRead, ZX_OK, ZX_SOCKET_SHUTDOWN_READ,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_socket_shutdown("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "options:\x1B[32mzx_socket_shutdown_options_t\x1B[0m: \x1B[34mZX_SOCKET_SHUTDOWN_READ\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

SOCKET_SHUTDOWN_DISPLAY_TEST(ZxSocketShutdownWrite, ZX_OK, ZX_SOCKET_SHUTDOWN_WRITE,
                             "\n"
                             "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                             "zx_socket_shutdown("
                             "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                             "options:\x1B[32mzx_socket_shutdown_options_t\x1B[0m: "
                             "\x1B[34mZX_SOCKET_SHUTDOWN_WRITE\x1B[0m)\n"
                             "  -> \x1B[32mZX_OK\x1B[0m\n");

}  // namespace fidlcat
