// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"

namespace fidlcat {

// zx_debuglog_create tests.

std::unique_ptr<SystemCallTest> ZxDebuglogCreate(int64_t result, std::string_view result_name,
                                                 zx_handle_t resource, uint32_t options,
                                                 zx_handle_t* out) {
  auto value = std::make_unique<SystemCallTest>("zx_debuglog_create", result, result_name);
  value->AddInput(resource);
  value->AddInput(options);
  value->AddInput(reinterpret_cast<uint64_t>(out));
  return value;
}

#define DEBUGLOG_CREATE_DISPLAY_TEST_CONTENT(result, expected) \
  zx_handle_t out = kHandleOut;                                \
  PerformDisplayTest("$plt(zx_debuglog_create)",               \
                     ZxDebuglogCreate(result, #result, kHandle, 0, &out), expected);

#define DEBUGLOG_CREATE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {               \
    DEBUGLOG_CREATE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                         \
  TEST_F(InterceptionWorkflowTestArm, name) {               \
    DEBUGLOG_CREATE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

DEBUGLOG_CREATE_DISPLAY_TEST(
    ZxDebuglogCreate, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_debuglog_create("
    "resource:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (out:\x1B[32mhandle\x1B[0m: \x1B[31mbde90caf\x1B[0m)\n");

// zx_debuglog_write tests.

std::unique_ptr<SystemCallTest> ZxDebuglogWrite(int64_t result, std::string_view result_name,
                                                zx_handle_t handle, uint32_t options,
                                                const void* buffer, size_t buffer_size) {
  auto value = std::make_unique<SystemCallTest>("zx_debuglog_write", result, result_name);
  value->AddInput(handle);
  value->AddInput(options);
  value->AddInput(reinterpret_cast<uint64_t>(buffer));
  value->AddInput(buffer_size);
  return value;
}

#define DEBUGLOG_WRITE_DISPLAY_TEST_CONTENT(result, expected)                                     \
  std::string buffer = "My buffer data";                                                          \
  PerformDisplayTest("$plt(zx_debuglog_write)",                                                   \
                     ZxDebuglogWrite(result, #result, kHandle, 0, buffer.c_str(), buffer.size()), \
                     expected);

#define DEBUGLOG_WRITE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {              \
    DEBUGLOG_WRITE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                        \
  TEST_F(InterceptionWorkflowTestArm, name) {              \
    DEBUGLOG_WRITE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

DEBUGLOG_WRITE_DISPLAY_TEST(ZxDebuglogWrite, ZX_OK,
                            "\n"
                            "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                            "zx_debuglog_write("
                            "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                            "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
                            "    buffer:\x1B[32muint8\x1B[0m: \x1B[31m\"My buffer data\"\x1B[0m\n"
                            "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_debuglog_read tests.

std::unique_ptr<SystemCallTest> ZxDebuglogRead(int64_t result, std::string_view result_name,
                                               zx_handle_t handle, uint32_t options, char* buffer,
                                               size_t buffer_size) {
  auto value = std::make_unique<SystemCallTest>("zx_debuglog_read", result, result_name);
  value->AddInput(handle);
  value->AddInput(options);
  value->AddInput(reinterpret_cast<uint64_t>(buffer));
  value->AddInput(buffer_size);
  return value;
}

#define DEBUGLOG_READ_DISPLAY_TEST_CONTENT(result, expected)                                    \
  std::string buffer = "My buffer data";                                                        \
  PerformDisplayTest("$plt(zx_debuglog_read)",                                                  \
                     ZxDebuglogRead(result, #result, kHandle, 0, buffer.data(), buffer.size()), \
                     expected);

#define DEBUGLOG_READ_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {             \
    DEBUGLOG_READ_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                       \
  TEST_F(InterceptionWorkflowTestArm, name) { DEBUGLOG_READ_DISPLAY_TEST_CONTENT(errno, expected); }

DEBUGLOG_READ_DISPLAY_TEST(
    ZxDebuglogRead, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_debuglog_read("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n"
    "      buffer:\x1B[32muint8\x1B[0m: \x1B[31m\"My buffer data\"\x1B[0m\n");

// zx_ktrace_read tests.

std::unique_ptr<SystemCallTest> ZxKtraceRead(int64_t result, std::string_view result_name,
                                             zx_handle_t handle, void* data, uint32_t offset,
                                             size_t data_size, size_t* actual) {
  auto value = std::make_unique<SystemCallTest>("zx_ktrace_read", result, result_name);
  value->AddInput(handle);
  value->AddInput(reinterpret_cast<uint64_t>(data));
  value->AddInput(offset);
  value->AddInput(data_size);
  value->AddInput(reinterpret_cast<uint64_t>(actual));
  return value;
}

#define KTRACE_READ_DISPLAY_TEST_CONTENT(result, expected)                                         \
  std::vector<char> data(100);                                                                     \
  std::string my_data = "My data";                                                                 \
  size_t actual = my_data.size();                                                                  \
  memcpy(data.data(), my_data.c_str(), my_data.size());                                            \
  PerformDisplayTest("$plt(zx_ktrace_read)",                                                       \
                     ZxKtraceRead(result, #result, kHandle, data.data(), 0, data.size(), &actual), \
                     expected);

#define KTRACE_READ_DISPLAY_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { KTRACE_READ_DISPLAY_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { KTRACE_READ_DISPLAY_TEST_CONTENT(errno, expected); }

KTRACE_READ_DISPLAY_TEST(ZxKtraceRead, ZX_OK,
                         "\n"
                         "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                         "zx_ktrace_read("
                         "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                         "offset:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
                         "  -> \x1B[32mZX_OK\x1B[0m ("
                         "actual:\x1B[32msize_t\x1B[0m: \x1B[34m7\x1B[0m/\x1B[34m100\x1B[0m)\n"
                         "      data:\x1B[32muint8\x1B[0m: \x1B[31m\"My data\"\x1B[0m\n");

// zx_ktrace_control tests.

std::unique_ptr<SystemCallTest> ZxKtraceControl(int64_t result, std::string_view result_name,
                                                zx_handle_t handle, uint32_t action,
                                                uint32_t options, void* ptr) {
  auto value = std::make_unique<SystemCallTest>("zx_ktrace_control", result, result_name);
  value->AddInput(handle);
  value->AddInput(action);
  value->AddInput(options);
  value->AddInput(reinterpret_cast<uint64_t>(ptr));
  return value;
}

#define KTRACE_CONTROL_DISPLAY_TEST_CONTENT(result, action, expected)                     \
  std::array<char, ZX_MAX_NAME_LEN> buffer;                                               \
  std::string data = "My_name";                                                           \
  memcpy(buffer.data(), data.c_str(), data.size() + 1);                                   \
  PerformDisplayTest("$plt(zx_ktrace_control)",                                           \
                     ZxKtraceControl(result, #result, kHandle, action, 0, buffer.data()), \
                     expected);

#define KTRACE_CONTROL_DISPLAY_TEST(name, errno, action, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                      \
    KTRACE_CONTROL_DISPLAY_TEST_CONTENT(errno, action, expected);  \
  }                                                                \
  TEST_F(InterceptionWorkflowTestArm, name) {                      \
    KTRACE_CONTROL_DISPLAY_TEST_CONTENT(errno, action, expected);  \
  }

KTRACE_CONTROL_DISPLAY_TEST(
    ZxKtraceControl1, ZX_OK, 1,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_ktrace_control("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "action:\x1B[32mzx_ktrace_control_action_t\x1B[0m: \x1B[34mKTRACE_ACTION_START\x1B[0m, "
    "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

KTRACE_CONTROL_DISPLAY_TEST(
    ZxKtraceControl4, ZX_OK, 4,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_ktrace_control("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "action:\x1B[32mzx_ktrace_control_action_t\x1B[0m: \x1B[34mKTRACE_ACTION_NEW_PROBE\x1B[0m, "
    "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m, "
    "ptr:\x1B[32mstring\x1B[0m: \x1B[31m\"My_name\"\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_ktrace_write tests.

std::unique_ptr<SystemCallTest> ZxKtraceWrite(int64_t result, std::string_view result_name,
                                              zx_handle_t handle, uint32_t id, uint32_t arg0,
                                              uint32_t arg1) {
  auto value = std::make_unique<SystemCallTest>("zx_ktrace_write", result, result_name);
  value->AddInput(handle);
  value->AddInput(id);
  value->AddInput(arg0);
  value->AddInput(arg1);
  return value;
}

#define KTRACE_WRITE_DISPLAY_TEST_CONTENT(result, expected)                                     \
  PerformDisplayTest("$plt(zx_ktrace_write)", ZxKtraceWrite(result, #result, kHandle, 0, 1, 2), \
                     expected);

#define KTRACE_WRITE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {            \
    KTRACE_WRITE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                      \
  TEST_F(InterceptionWorkflowTestArm, name) { KTRACE_WRITE_DISPLAY_TEST_CONTENT(errno, expected); }

KTRACE_WRITE_DISPLAY_TEST(ZxKtraceWrite, ZX_OK,
                          "\n"
                          "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                          "zx_ktrace_write("
                          "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                          "id:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m, "
                          "arg0:\x1B[32muint32\x1B[0m: \x1B[34m1\x1B[0m, "
                          "arg1:\x1B[32muint32\x1B[0m: \x1B[34m2\x1B[0m)\n"
                          "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_mtrace_control tests.

std::unique_ptr<SystemCallTest> ZxMtraceControl(int64_t result, std::string_view result_name,
                                                zx_handle_t handle, uint32_t kind, uint32_t action,
                                                uint32_t options, const void* ptr,
                                                size_t ptr_size) {
  auto value = std::make_unique<SystemCallTest>("zx_mtrace_control", result, result_name);
  value->AddInput(handle);
  value->AddInput(kind);
  value->AddInput(action);
  value->AddInput(options);
  value->AddInput(reinterpret_cast<uint64_t>(ptr));
  value->AddInput(ptr_size);
  return value;
}

#define MTRACE_CONTROL_DISPLAY_TEST_CONTENT(result, expected) \
  std::string data = "My data";                               \
  PerformDisplayTest(                                         \
      "$plt(zx_mtrace_control)",                              \
      ZxMtraceControl(result, #result, kHandle, 1, 2, 3, data.c_str(), data.size()), expected);

#define MTRACE_CONTROL_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {              \
    MTRACE_CONTROL_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                        \
  TEST_F(InterceptionWorkflowTestArm, name) {              \
    MTRACE_CONTROL_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

MTRACE_CONTROL_DISPLAY_TEST(ZxMtraceControl, ZX_OK,
                            "\n"
                            "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                            "zx_mtrace_control("
                            "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                            "kind:\x1B[32muint32\x1B[0m: \x1B[34m1\x1B[0m, "
                            "action:\x1B[32muint32\x1B[0m: \x1B[34m2\x1B[0m, "
                            "options:\x1B[32muint32\x1B[0m: \x1B[34m3\x1B[0m)\n"
                            "    ptr:\x1B[32muint8\x1B[0m: \x1B[31m\"My data\"\x1B[0m\n"
                            "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_debug_read tests.

std::unique_ptr<SystemCallTest> ZxDebugRead(int64_t result, std::string_view result_name,
                                            zx_handle_t handle, char* buffer, size_t buffer_size,
                                            size_t* actual) {
  auto value = std::make_unique<SystemCallTest>("zx_debug_read", result, result_name);
  value->AddInput(handle);
  value->AddInput(reinterpret_cast<uint64_t>(buffer));
  value->AddInput(buffer_size);
  value->AddInput(reinterpret_cast<uint64_t>(actual));
  return value;
}

#define DEBUG_READ_DISPLAY_TEST_CONTENT(result, expected)                                          \
  std::array<char, ZX_MAX_NAME_LEN> buffer;                                                        \
  std::string data = "My data";                                                                    \
  memcpy(buffer.data(), data.c_str(), data.size());                                                \
  size_t actual = data.size();                                                                     \
  PerformDisplayTest("$plt(zx_debug_read)",                                                        \
                     ZxDebugRead(result, #result, kHandle, buffer.data(), buffer.size(), &actual), \
                     expected);

#define DEBUG_READ_DISPLAY_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { DEBUG_READ_DISPLAY_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { DEBUG_READ_DISPLAY_TEST_CONTENT(errno, expected); }

DEBUG_READ_DISPLAY_TEST(ZxDebugRead, ZX_OK,
                        "\n"
                        "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                        "zx_debug_read(handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m)\n"
                        "  -> \x1B[32mZX_OK\x1B[0m ("
                        "actual:\x1B[32msize_t\x1B[0m: \x1B[34m7\x1B[0m/\x1B[34m32\x1B[0m, "
                        "buffer:\x1B[32mstring\x1B[0m: \x1B[31m\"My data\"\x1B[0m)\n");

// zx_debug_write tests.

std::unique_ptr<SystemCallTest> ZxDebugWrite(int64_t result, std::string_view result_name,
                                             const char* buffer, size_t buffer_size) {
  auto value = std::make_unique<SystemCallTest>("zx_debug_write", result, result_name);
  value->AddInput(reinterpret_cast<uint64_t>(buffer));
  value->AddInput(buffer_size);
  return value;
}

#define DEBUG_WRITE_DISPLAY_TEST_CONTENT(result, expected) \
  std::string buffer = "My data";                          \
  PerformDisplayTest("$plt(zx_debug_write)",               \
                     ZxDebugWrite(result, #result, buffer.data(), buffer.size()), expected);

#define DEBUG_WRITE_DISPLAY_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { DEBUG_WRITE_DISPLAY_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { DEBUG_WRITE_DISPLAY_TEST_CONTENT(errno, expected); }

DEBUG_WRITE_DISPLAY_TEST(
    ZxDebugWrite, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_debug_write(buffer:\x1B[32mstring\x1B[0m: \x1B[31m\"My data\"\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_debug_send_command tests.

std::unique_ptr<SystemCallTest> ZxDebugSendCommand(int64_t result, std::string_view result_name,
                                                   zx_handle_t resource, const char* buffer,
                                                   size_t buffer_size) {
  auto value = std::make_unique<SystemCallTest>("zx_debug_send_command", result, result_name);
  value->AddInput(resource);
  value->AddInput(reinterpret_cast<uint64_t>(buffer));
  value->AddInput(buffer_size);
  return value;
}

#define DEBUG_SEND_COMMAND_DISPLAY_TEST_CONTENT(result, expected)                                \
  std::string buffer = "My data";                                                                \
  PerformDisplayTest("$plt(zx_debug_send_command)",                                              \
                     ZxDebugSendCommand(result, #result, kHandle, buffer.data(), buffer.size()), \
                     expected);

#define DEBUG_SEND_COMMAND_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                  \
    DEBUG_SEND_COMMAND_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                            \
  TEST_F(InterceptionWorkflowTestArm, name) {                  \
    DEBUG_SEND_COMMAND_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

DEBUG_SEND_COMMAND_DISPLAY_TEST(ZxDebugSendCommand, ZX_OK,
                                "\n"
                                "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                                "zx_debug_send_command("
                                "resource:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                                "buffer:\x1B[32mstring\x1B[0m: \x1B[31m\"My data\"\x1B[0m)\n"
                                "  -> \x1B[32mZX_OK\x1B[0m\n");

}  // namespace fidlcat
