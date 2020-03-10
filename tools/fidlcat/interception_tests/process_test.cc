// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"

namespace fidlcat {

// zx_process_exit tests.

std::unique_ptr<SystemCallTest> ZxProcessExit(int64_t result, std::string_view result_name,
                                              int64_t retcode) {
  auto value = std::make_unique<SystemCallTest>("zx_process_exit", result, result_name);
  value->AddInput(retcode);
  return value;
}

#define PROCESS_EXIT_DISPLAY_TEST_CONTENT(result, retcode, expected)                           \
  PerformNoReturnDisplayTest("$plt(zx_process_exit)", ZxProcessExit(result, #result, retcode), \
                             expected);

#define PROCESS_EXIT_DISPLAY_TEST(name, errno, retcode, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                     \
    PROCESS_EXIT_DISPLAY_TEST_CONTENT(errno, retcode, expected);  \
  }                                                               \
  TEST_F(InterceptionWorkflowTestArm, name) {                     \
    PROCESS_EXIT_DISPLAY_TEST_CONTENT(errno, retcode, expected);  \
  }

PROCESS_EXIT_DISPLAY_TEST(ZxProcessExit0, ZX_OK, 0,
                          "\n"
                          "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                          "zx_process_exit(retcode:\x1B[32mint64\x1B[0m: \x1B[34m0\x1B[0m)\n");

PROCESS_EXIT_DISPLAY_TEST(ZxProcessExit1, ZX_OK, 1,
                          "\n"
                          "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                          "zx_process_exit(retcode:\x1B[32mint64\x1B[0m: \x1B[34m1\x1B[0m)\n");

// zx_process_create tests.

std::unique_ptr<SystemCallTest> ZxProcessCreate(int64_t result, std::string_view result_name,
                                                zx_handle_t job, const char* name, size_t name_size,
                                                uint32_t options, zx_handle_t* proc_handle,
                                                zx_handle_t* vmar_handle) {
  auto value = std::make_unique<SystemCallTest>("zx_process_create", result, result_name);
  value->AddInput(job);
  value->AddInput(reinterpret_cast<uint64_t>(name));
  value->AddInput(name_size);
  value->AddInput(options);
  value->AddInput(reinterpret_cast<uint64_t>(proc_handle));
  value->AddInput(reinterpret_cast<uint64_t>(vmar_handle));
  return value;
}

#define PROCESS_CREATE_DISPLAY_TEST_CONTENT(result, expected)                                \
  std::string name("my_process");                                                            \
  zx_handle_t proc_handle = kHandleOut;                                                      \
  zx_handle_t vmar_handle = kHandleOut2;                                                     \
  PerformDisplayTest("$plt(zx_process_create)",                                              \
                     ZxProcessCreate(result, #result, kHandle, name.c_str(), name.size(), 0, \
                                     &proc_handle, &vmar_handle),                            \
                     expected);

#define PROCESS_CREATE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {              \
    PROCESS_CREATE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                        \
  TEST_F(InterceptionWorkflowTestArm, name) {              \
    PROCESS_CREATE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

PROCESS_CREATE_DISPLAY_TEST(ZxProcessCreate, ZX_OK,
                            "\n"
                            "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                            "zx_process_create("
                            "job:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                            "name:\x1B[32mstring\x1B[0m: \x1B[31m\"my_process\"\x1B[0m, "
                            "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
                            "  -> \x1B[32mZX_OK\x1B[0m ("
                            "proc_handle:\x1B[32mhandle\x1B[0m: \x1B[31mbde90caf\x1B[0m, "
                            "vmar_handle:\x1B[32mhandle\x1B[0m: \x1B[31mbde90222\x1B[0m)\n");

// zx_process_start tests.

std::unique_ptr<SystemCallTest> ZxProcessStart(int64_t result, std::string_view result_name,
                                               zx_handle_t handle, zx_handle_t thread,
                                               zx_vaddr_t entry, zx_vaddr_t stack, zx_handle_t arg1,
                                               uintptr_t arg2) {
  auto value = std::make_unique<SystemCallTest>("zx_process_start", result, result_name);
  value->AddInput(handle);
  value->AddInput(thread);
  value->AddInput(entry);
  value->AddInput(stack);
  value->AddInput(arg1);
  value->AddInput(arg2);
  return value;
}

#define PROCESS_START_DISPLAY_TEST_CONTENT(result, expected) \
  zx_vaddr_t entry = 0x123456;                               \
  zx_vaddr_t stack = 0x100001234;                            \
  uintptr_t arg2 = 0x789abcdef;                              \
  PerformDisplayTest(                                        \
      "$plt(zx_process_start)",                              \
      ZxProcessStart(result, #result, kHandle, kHandle2, entry, stack, kHandle3, arg2), expected);

#define PROCESS_START_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {             \
    PROCESS_START_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                       \
  TEST_F(InterceptionWorkflowTestArm, name) { PROCESS_START_DISPLAY_TEST_CONTENT(errno, expected); }

PROCESS_START_DISPLAY_TEST(ZxProcessStart, ZX_OK,
                           "\n"
                           "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                           "zx_process_start("
                           "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                           "thread:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1222\x1B[0m, "
                           "entry:\x1B[32mzx_vaddr_t\x1B[0m: \x1B[34m0000000000123456\x1B[0m, "
                           "stack:\x1B[32mzx_vaddr_t\x1B[0m: \x1B[34m0000000100001234\x1B[0m, "
                           "arg1:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1333\x1B[0m, "
                           "arg2:\x1B[32muintptr_t\x1B[0m: \x1B[34m0000000789abcdef\x1B[0m)\n"
                           "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_process_read_memory tests.

std::unique_ptr<SystemCallTest> ZxProcessReadMemory(int64_t result, std::string_view result_name,
                                                    zx_handle_t handle, zx_vaddr_t vaddr,
                                                    void* buffer, size_t buffer_size,
                                                    size_t* actual) {
  auto value = std::make_unique<SystemCallTest>("zx_process_read_memory", result, result_name);
  value->AddInput(handle);
  value->AddInput(vaddr);
  value->AddInput(reinterpret_cast<uint64_t>(buffer));
  value->AddInput(buffer_size);
  value->AddInput(reinterpret_cast<uint64_t>(actual));
  return value;
}

#define PROCESS_READ_MEMORY_DISPLAY_TEST_CONTENT(result, expected)                                 \
  zx_vaddr_t vaddr = 0x123456789;                                                                  \
  std::vector<uint8_t> buffer;                                                                     \
  for (int i = 0; i < 10; ++i) {                                                                   \
    buffer.push_back(i);                                                                           \
  }                                                                                                \
  size_t actual = buffer.size();                                                                   \
  PerformDisplayTest(                                                                              \
      "$plt(zx_process_read_memory)",                                                              \
      ZxProcessReadMemory(result, #result, kHandle, vaddr, buffer.data(), buffer.size(), &actual), \
      expected);

#define PROCESS_READ_MEMORY_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                   \
    PROCESS_READ_MEMORY_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                             \
  TEST_F(InterceptionWorkflowTestArm, name) {                   \
    PROCESS_READ_MEMORY_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

PROCESS_READ_MEMORY_DISPLAY_TEST(
    ZxProcessReadMemory, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_process_read_memory("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "vaddr:\x1B[32mzx_vaddr_t\x1B[0m: \x1B[34m0000000123456789\x1B[0m, "
    "buffer_size:\x1B[32msize_t\x1B[0m: \x1B[34m10\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n"
    "      buffer:\x1B[32muint8\x1B[0m: "
    "\x1B[34m0\x1B[0m, \x1B[34m1\x1B[0m, \x1B[34m2\x1B[0m, \x1B[34m3\x1B[0m, \x1B[34m4\x1B[0m, "
    "\x1B[34m5\x1B[0m, \x1B[34m6\x1B[0m, \x1B[34m7\x1B[0m, \x1B[34m8\x1B[0m, \x1B[34m9\x1B[0m\n");

// zx_process_write_memory tests.

std::unique_ptr<SystemCallTest> ZxProcessWriteMemory(int64_t result, std::string_view result_name,
                                                     zx_handle_t handle, zx_vaddr_t vaddr,
                                                     const void* buffer, size_t buffer_size,
                                                     size_t* actual) {
  auto value = std::make_unique<SystemCallTest>("zx_process_write_memory", result, result_name);
  value->AddInput(handle);
  value->AddInput(vaddr);
  value->AddInput(reinterpret_cast<uint64_t>(buffer));
  value->AddInput(buffer_size);
  value->AddInput(reinterpret_cast<uint64_t>(actual));
  return value;
}

#define PROCESS_WRITE_MEMORY_DISPLAY_TEST_CONTENT(result, expected)                       \
  zx_vaddr_t vaddr = 0x123456789;                                                         \
  std::vector<uint8_t> buffer;                                                            \
  for (int i = 0; i < 10; ++i) {                                                          \
    buffer.push_back(i);                                                                  \
  }                                                                                       \
  size_t actual = buffer.size();                                                          \
  PerformDisplayTest("$plt(zx_process_write_memory)",                                     \
                     ZxProcessWriteMemory(result, #result, kHandle, vaddr, buffer.data(), \
                                          buffer.size(), &actual),                        \
                     expected);

#define PROCESS_WRITE_MEMORY_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                    \
    PROCESS_WRITE_MEMORY_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                              \
  TEST_F(InterceptionWorkflowTestArm, name) {                    \
    PROCESS_WRITE_MEMORY_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

PROCESS_WRITE_MEMORY_DISPLAY_TEST(
    ZxProcessWriteMemory, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_process_write_memory("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "vaddr:\x1B[32mzx_vaddr_t\x1B[0m: \x1B[34m0000000123456789\x1B[0m)\n"
    "    buffer:\x1B[32muint8\x1B[0m: "
    "\x1B[34m0\x1B[0m, \x1B[34m1\x1B[0m, \x1B[34m2\x1B[0m, \x1B[34m3\x1B[0m, \x1B[34m4\x1B[0m, "
    "\x1B[34m5\x1B[0m, \x1B[34m6\x1B[0m, \x1B[34m7\x1B[0m, \x1B[34m8\x1B[0m, \x1B[34m9\x1B[0m\n"
    "  -> \x1B[32mZX_OK\x1B[0m (actual:\x1B[32msize_t\x1B[0m: \x1B[34m10\x1B[0m)\n");

}  // namespace fidlcat
