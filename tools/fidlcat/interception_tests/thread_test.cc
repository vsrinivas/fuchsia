// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"

namespace fidlcat {

// zx_thread_exit tests.

std::unique_ptr<SystemCallTest> ZxThreadExit() {
  return std::make_unique<SystemCallTest>("zx_thread_exit", 0, "");
}

#define THREAD_EXIT_DISPLAY_TEST_CONTENT(expected) \
  PerformNoReturnDisplayTest("$plt(zx_thread_exit)", ZxThreadExit(), expected);

#define THREAD_EXIT_DISPLAY_TEST(name, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { THREAD_EXIT_DISPLAY_TEST_CONTENT(expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { THREAD_EXIT_DISPLAY_TEST_CONTENT(expected); }

THREAD_EXIT_DISPLAY_TEST(ZxThreadExit,
                         "\n"
                         "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                         "zx_thread_exit()\n");

// zx_thread_create tests.

std::unique_ptr<SystemCallTest> ZxThreadCreate(int64_t result, std::string_view result_name,
                                               zx_handle_t process, const char* name,
                                               size_t name_size, uint32_t options,
                                               zx_handle_t* out) {
  auto value = std::make_unique<SystemCallTest>("zx_thread_create", result, result_name);
  value->AddInput(process);
  value->AddInput(reinterpret_cast<uint64_t>(name));
  value->AddInput(name_size);
  value->AddInput(options);
  value->AddInput(reinterpret_cast<uint64_t>(out));
  return value;
}

#define THREAD_CREATE_DISPLAY_TEST_CONTENT(result, expected)                                \
  const char* name = "my_thread";                                                           \
  zx_handle_t out = kHandleOut;                                                             \
  PerformDisplayTest("$plt(zx_thread_create)",                                              \
                     ZxThreadCreate(result, #result, kHandle, name, strlen(name), 0, &out), \
                     expected);

#define THREAD_CREATE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {             \
    THREAD_CREATE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                       \
  TEST_F(InterceptionWorkflowTestArm, name) { THREAD_CREATE_DISPLAY_TEST_CONTENT(errno, expected); }

THREAD_CREATE_DISPLAY_TEST(ZxThreadCreate, ZX_OK,
                           "\n"
                           "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                           "zx_thread_create("
                           "process:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                           "name:\x1B[32mstring\x1B[0m: \x1B[31m\"my_thread\"\x1B[0m, "
                           "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
                           "  -> \x1B[32mZX_OK\x1B[0m ("
                           "out:\x1B[32mhandle\x1B[0m: \x1B[31mbde90caf\x1B[0m)\n");

// zx_thread_start tests.

std::unique_ptr<SystemCallTest> ZxThreadStart(int64_t result, std::string_view result_name,
                                              zx_handle_t handle, zx_vaddr_t thread_entry,
                                              zx_vaddr_t stack, uintptr_t arg1, uintptr_t arg2) {
  auto value = std::make_unique<SystemCallTest>("zx_thread_start", result, result_name);
  value->AddInput(handle);
  value->AddInput(thread_entry);
  value->AddInput(stack);
  value->AddInput(arg1);
  value->AddInput(arg2);
  return value;
}

#define THREAD_START_DISPLAY_TEST_CONTENT(result, expected)                                    \
  zx_vaddr_t thread_entry = 0xeeee;                                                            \
  zx_vaddr_t stack = 0xaaaa;                                                                   \
  uintptr_t arg1 = 0x1111;                                                                     \
  uintptr_t arg2 = 0x2222;                                                                     \
  PerformDisplayTest("$plt(zx_thread_start)",                                                  \
                     ZxThreadStart(result, #result, kHandle, thread_entry, stack, arg1, arg2), \
                     expected);

#define THREAD_START_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {            \
    THREAD_START_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                      \
  TEST_F(InterceptionWorkflowTestArm, name) { THREAD_START_DISPLAY_TEST_CONTENT(errno, expected); }

THREAD_START_DISPLAY_TEST(
    ZxThreadStart, ZX_OK,
    "\ntest_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_thread_start("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "thread_entry:\x1B[32mzx_vaddr_t\x1B[0m: \x1B[34m000000000000eeee\x1B[0m, "
    "stack:\x1B[32mzx_vaddr_t\x1B[0m: \x1B[34m000000000000aaaa\x1B[0m, "
    "arg1:\x1B[32muintptr_t\x1B[0m: \x1B[34m0000000000001111\x1B[0m, "
    "arg2:\x1B[32muintptr_t\x1B[0m: \x1B[34m0000000000002222\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_thread_read_state tests.

std::unique_ptr<SystemCallTest> ZxThreadReadState(int64_t result, std::string_view result_name,
                                                  zx_handle_t handle, uint32_t kind, void* buffer,
                                                  size_t buffer_size) {
  auto value = std::make_unique<SystemCallTest>("zx_thread_read_state", result, result_name);
  value->AddInput(handle);
  value->AddInput(kind);
  value->AddInput(reinterpret_cast<uint64_t>(buffer));
  value->AddInput(buffer_size);
  return value;
}

#define THREAD_READ_STATE_DISPLAY_TEST_CONTENT(result, kind, buffer, expected)                   \
  PerformDisplayTest("$plt(zx_thread_read_state)",                                               \
                     ZxThreadReadState(result, #result, kHandle, kind, &buffer, sizeof(buffer)), \
                     expected);

TEST_F(InterceptionWorkflowTestArm, ZxThreadReadStateGeneralRegsAArch64) {
  zx_thread_state_general_regs_aarch64_t buffer;
  memset(&buffer, 0, sizeof(buffer));
  constexpr uint64_t kIncrement = 0x100000001UL;
  uint64_t value = 0;
  for (size_t i = 0; i < sizeof(buffer.r) / sizeof(buffer.r[0]); ++i) {
    buffer.r[i] = value;
    value += kIncrement;
  }
  buffer.lr = 0x11111111;
  buffer.sp = 0x22222222;
  buffer.pc = 0xcccccccc;
  buffer.cpsr = 0xdddddddd;
  buffer.tpidr = 0xeeeeeeee;
  THREAD_READ_STATE_DISPLAY_TEST_CONTENT(
      ZX_OK, ZX_THREAD_STATE_GENERAL_REGS, buffer,
      "\n"
      "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
      "zx_thread_read_state("
      "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
      "kind:\x1B[32mzx_thread_state_topic_t\x1B[0m: \x1B[34mZX_THREAD_STATE_GENERAL_REGS\x1B[0m, "
      "buffer_size:\x1B[32msize_t\x1B[0m: \x1B[34m280\x1B[0m)\n"
      "  -> \x1B[32mZX_OK\x1B[0m\n"
      "      regs:\x1B[32mzx_thread_state_general_regs_aarch64_t\x1B[0m: {\n"
      "        r:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000000000000000\x1B[0m, \x1B[34m0000000100000001\x1B[0m, "
      "\x1B[34m0000000200000002\x1B[0m, \x1B[34m0000000300000003\x1B[0m, "
      "\x1B[34m0000000400000004\x1B[0m, \x1B[34m0000000500000005\x1B[0m, "
      "\x1B[34m0000000600000006\x1B[0m, \x1B[34m0000000700000007\x1B[0m, "
      "\x1B[34m0000000800000008\x1B[0m, \x1B[34m0000000900000009\x1B[0m, "
      "\x1B[34m0000000a0000000a\x1B[0m, \x1B[34m0000000b0000000b\x1B[0m, "
      "\x1B[34m0000000c0000000c\x1B[0m, \x1B[34m0000000d0000000d\x1B[0m, "
      "\x1B[34m0000000e0000000e\x1B[0m, \x1B[34m0000000f0000000f\x1B[0m, "
      "\x1B[34m0000001000000010\x1B[0m, \x1B[34m0000001100000011\x1B[0m, "
      "\x1B[34m0000001200000012\x1B[0m, \x1B[34m0000001300000013\x1B[0m, "
      "\x1B[34m0000001400000014\x1B[0m, \x1B[34m0000001500000015\x1B[0m, "
      "\x1B[34m0000001600000016\x1B[0m, \x1B[34m0000001700000017\x1B[0m, "
      "\x1B[34m0000001800000018\x1B[0m, \x1B[34m0000001900000019\x1B[0m, "
      "\x1B[34m0000001a0000001a\x1B[0m, \x1B[34m0000001b0000001b\x1B[0m, "
      "\x1B[34m0000001c0000001c\x1B[0m, \x1B[34m0000001d0000001d\x1B[0m\n"
      "        lr:\x1B[32muint64\x1B[0m: \x1B[34m0000000011111111\x1B[0m\n"
      "        sp:\x1B[32muint64\x1B[0m: \x1B[34m0000000022222222\x1B[0m\n"
      "        pc:\x1B[32muint64\x1B[0m: \x1B[34m00000000cccccccc\x1B[0m\n"
      "        cpsr:\x1B[32muint64\x1B[0m: \x1B[34m00000000dddddddd\x1B[0m\n"
      "        tpidr:\x1B[32muint64\x1B[0m: \x1B[34m00000000eeeeeeee\x1B[0m\n"
      "      }\n");
}

TEST_F(InterceptionWorkflowTestX64, ZxThreadReadStateGeneralRegsX64) {
  zx_thread_state_general_regs_x86_t buffer;
  memset(&buffer, 0, sizeof(buffer));
  buffer.rax = 0xaaaa;
  buffer.rbx = 0xbbbb;
  buffer.rcx = 0xcccc;
  buffer.rdx = 0xdddd;
  buffer.rsi = 0x1234;
  buffer.rbp = 0x2345;
  buffer.rsp = 0x3456;
  buffer.r8 = 0x0808;
  buffer.r9 = 0x0909;
  buffer.r10 = 0x1010;
  buffer.r11 = 0x1111;
  buffer.r12 = 0x1212;
  buffer.r13 = 0x1313;
  buffer.r14 = 0x1414;
  buffer.r15 = 0x1515;
  buffer.rip = 0x1111;
  buffer.rflags = 0;
  buffer.fs_base = 0x100000000UL;
  buffer.gs_base = 0x200000000UL;
  THREAD_READ_STATE_DISPLAY_TEST_CONTENT(
      ZX_OK, ZX_THREAD_STATE_GENERAL_REGS, buffer,
      "\n"
      "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
      "zx_thread_read_state("
      "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
      "kind:\x1B[32mzx_thread_state_topic_t\x1B[0m: \x1B[34mZX_THREAD_STATE_GENERAL_REGS\x1B[0m, "
      "buffer_size:\x1B[32msize_t\x1B[0m: \x1B[34m160\x1B[0m)\n"
      "  -> \x1B[32mZX_OK\x1B[0m\n"
      "      regs:\x1B[32mzx_thread_state_general_regs_x86_t\x1B[0m: {\n"
      "        rax:\x1B[32muint64\x1B[0m: \x1B[34m000000000000aaaa\x1B[0m\n"
      "        rbx:\x1B[32muint64\x1B[0m: \x1B[34m000000000000bbbb\x1B[0m\n"
      "        rcx:\x1B[32muint64\x1B[0m: \x1B[34m000000000000cccc\x1B[0m\n"
      "        rdx:\x1B[32muint64\x1B[0m: \x1B[34m000000000000dddd\x1B[0m\n"
      "        rsi:\x1B[32muint64\x1B[0m: \x1B[34m0000000000001234\x1B[0m\n"
      "        rdi:\x1B[32muint64\x1B[0m: \x1B[34m0000000000000000\x1B[0m\n"
      "        rbp:\x1B[32muint64\x1B[0m: \x1B[34m0000000000002345\x1B[0m\n"
      "        rsp:\x1B[32muint64\x1B[0m: \x1B[34m0000000000003456\x1B[0m\n"
      "        r8:\x1B[32muint64\x1B[0m: \x1B[34m0000000000000808\x1B[0m\n"
      "        r9:\x1B[32muint64\x1B[0m: \x1B[34m0000000000000909\x1B[0m\n"
      "        r10:\x1B[32muint64\x1B[0m: \x1B[34m0000000000001010\x1B[0m\n"
      "        r11:\x1B[32muint64\x1B[0m: \x1B[34m0000000000001111\x1B[0m\n"
      "        r12:\x1B[32muint64\x1B[0m: \x1B[34m0000000000001212\x1B[0m\n"
      "        r13:\x1B[32muint64\x1B[0m: \x1B[34m0000000000001313\x1B[0m\n"
      "        r14:\x1B[32muint64\x1B[0m: \x1B[34m0000000000001414\x1B[0m\n"
      "        r15:\x1B[32muint64\x1B[0m: \x1B[34m0000000000001515\x1B[0m\n"
      "        rip:\x1B[32muint64\x1B[0m: \x1B[34m0000000000001111\x1B[0m\n"
      "        rflags:\x1B[32muint64\x1B[0m: \x1B[34m0000000000000000\x1B[0m\n"
      "        fs_base:\x1B[32muint64\x1B[0m: \x1B[34m0000000100000000\x1B[0m\n"
      "        gs_base:\x1B[32muint64\x1B[0m: \x1B[34m0000000200000000\x1B[0m\n"
      "      }\n");
}

TEST_F(InterceptionWorkflowTestX64, ZxThreadReadStateFpRegsX64) {
  zx_thread_state_fp_regs_x86_t buffer;
  memset(&buffer, 0, sizeof(buffer));
  buffer.fcw = 0xcccc;
  buffer.fsw = 0xdddd;
  buffer.ftw = 0xee;
  buffer.fop = 0xffff;
  buffer.fip = 0x100000001UL;
  buffer.fdp = 0xd0000000dUL;
  constexpr uint64_t kLowIncrement = 0x100000001UL;
  constexpr uint64_t kHighIncrement = 0x100000000UL;
  uint64_t low = 0;
  uint64_t high = 0;
  for (size_t i = 0; i < sizeof(buffer.st) / sizeof(buffer.st[0]); ++i) {
    buffer.st[i].low = low;
    buffer.st[i].high = high;
    low += kLowIncrement;
    high += kHighIncrement;
  }
  THREAD_READ_STATE_DISPLAY_TEST_CONTENT(
      ZX_OK, ZX_THREAD_STATE_FP_REGS, buffer,
      "\n"
      "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
      "zx_thread_read_state("
      "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
      "kind:\x1B[32mzx_thread_state_topic_t\x1B[0m: \x1B[34mZX_THREAD_STATE_FP_REGS\x1B[0m, "
      "buffer_size:\x1B[32msize_t\x1B[0m: \x1B[34m160\x1B[0m)\n"
      "  -> \x1B[32mZX_OK\x1B[0m\n"
      "      regs:\x1B[32mzx_thread_state_fp_regs_x86_t\x1B[0m: {\n"
      "        fcw:\x1B[32muint16\x1B[0m: \x1B[34mcccc\x1B[0m\n"
      "        fsw:\x1B[32muint16\x1B[0m: \x1B[34mdddd\x1B[0m\n"
      "        ftw:\x1B[32muint8\x1B[0m: \x1B[34mee\x1B[0m\n"
      "        fop:\x1B[32muint16\x1B[0m: \x1B[34mffff\x1B[0m\n"
      "        fip:\x1B[32muint64\x1B[0m: \x1B[34m0000000100000001\x1B[0m\n"
      "        fdp:\x1B[32muint64\x1B[0m: \x1B[34m0000000d0000000d\x1B[0m\n"
      "        st:\x1B[32muint128[]\x1B[0m: "
      "\x1B[34m{ low = 0000000000000000, high = 0000000000000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000100000001, high = 0000000100000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000200000002, high = 0000000200000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000300000003, high = 0000000300000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000400000004, high = 0000000400000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000500000005, high = 0000000500000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000600000006, high = 0000000600000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000700000007, high = 0000000700000000 }\x1B[0m\n"
      "      }\n");
}

TEST_F(InterceptionWorkflowTestArm, ZxThreadReadStateVectorRegsAArch64) {
  zx_thread_state_vector_regs_aarch64_t buffer;
  memset(&buffer, 0, sizeof(buffer));
  buffer.fpcr = 0x12345678U;
  buffer.fpsr = 0x87654321U;
  constexpr uint64_t kLowIncrement = 0x100000001UL;
  constexpr uint64_t kHighIncrement = 0x100000000UL;
  uint64_t low = 0;
  uint64_t high = 0;
  for (size_t i = 0; i < sizeof(buffer.v) / sizeof(buffer.v[0]); ++i) {
    buffer.v[i].low = low;
    buffer.v[i].high = high;
    low += kLowIncrement;
    high += kHighIncrement;
  }
  THREAD_READ_STATE_DISPLAY_TEST_CONTENT(
      ZX_OK, ZX_THREAD_STATE_VECTOR_REGS, buffer,
      "\n"
      "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
      "zx_thread_read_state("
      "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
      "kind:\x1B[32mzx_thread_state_topic_t\x1B[0m: \x1B[34mZX_THREAD_STATE_VECTOR_REGS\x1B[0m, "
      "buffer_size:\x1B[32msize_t\x1B[0m: \x1B[34m520\x1B[0m)\n"
      "  -> \x1B[32mZX_OK\x1B[0m\n"
      "      regs:\x1B[32mzx_thread_state_vector_regs_aarch64_t\x1B[0m: {\n"
      "        fpcr:\x1B[32muint32\x1B[0m: \x1B[34m12345678\x1B[0m\n"
      "        fpsr:\x1B[32muint32\x1B[0m: \x1B[34m87654321\x1B[0m\n"
      "        v:\x1B[32muint128[]\x1B[0m: "
      "\x1B[34m{ low = 0000000000000000, high = 0000000000000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000100000001, high = 0000000100000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000200000002, high = 0000000200000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000300000003, high = 0000000300000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000400000004, high = 0000000400000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000500000005, high = 0000000500000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000600000006, high = 0000000600000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000700000007, high = 0000000700000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000800000008, high = 0000000800000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000900000009, high = 0000000900000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000a0000000a, high = 0000000a00000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000b0000000b, high = 0000000b00000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000c0000000c, high = 0000000c00000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000d0000000d, high = 0000000d00000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000e0000000e, high = 0000000e00000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000f0000000f, high = 0000000f00000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000001000000010, high = 0000001000000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000001100000011, high = 0000001100000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000001200000012, high = 0000001200000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000001300000013, high = 0000001300000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000001400000014, high = 0000001400000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000001500000015, high = 0000001500000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000001600000016, high = 0000001600000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000001700000017, high = 0000001700000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000001800000018, high = 0000001800000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000001900000019, high = 0000001900000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000001a0000001a, high = 0000001a00000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000001b0000001b, high = 0000001b00000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000001c0000001c, high = 0000001c00000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000001d0000001d, high = 0000001d00000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000001e0000001e, high = 0000001e00000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000001f0000001f, high = 0000001f00000000 }\x1B[0m\n"
      "      }\n");
}

TEST_F(InterceptionWorkflowTestX64, ZxThreadReadStateVectorRegsX64) {
  zx_thread_state_vector_regs_x86_t buffer;
  memset(&buffer, 0, sizeof(buffer));
  constexpr uint64_t kIncrement = 0x100000001UL;
  uint64_t value = 0;
  for (size_t i = 0; i < sizeof(buffer.zmm) / sizeof(buffer.zmm[0]); ++i) {
    for (size_t j = 0; j < sizeof(buffer.zmm[0].v) / sizeof(buffer.zmm[0].v[0]); ++j) {
      buffer.zmm[i].v[j] = value;
      value += kIncrement;
    }
  }
  for (size_t i = 0; i < sizeof(buffer.opmask) / sizeof(buffer.opmask[0]); ++i) {
    buffer.opmask[i] = value;
    value += kIncrement;
  }
  buffer.mxcsr = 0x12345678;
  THREAD_READ_STATE_DISPLAY_TEST_CONTENT(
      ZX_OK, ZX_THREAD_STATE_VECTOR_REGS, buffer,
      "\n"
      "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
      "zx_thread_read_state("
      "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
      "kind:\x1B[32mzx_thread_state_topic_t\x1B[0m: \x1B[34mZX_THREAD_STATE_VECTOR_REGS\x1B[0m, "
      "buffer_size:\x1B[32msize_t\x1B[0m: \x1B[34m2120\x1B[0m)\n"
      "  -> \x1B[32mZX_OK\x1B[0m\n"
      "      regs:\x1B[32mzx_thread_state_vector_regs_x86_t\x1B[0m: {\n"
      "        zmm:\x1B[32mzx_thread_state_vector_regs_x86_zmm_t\x1B[0m[]: {\n"
      "          {\n"
      "            v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000000000000000\x1B[0m, \x1B[34m0000000100000001\x1B[0m, "
      "\x1B[34m0000000200000002\x1B[0m, \x1B[34m0000000300000003\x1B[0m, "
      "\x1B[34m0000000400000004\x1B[0m, \x1B[34m0000000500000005\x1B[0m, "
      "\x1B[34m0000000600000006\x1B[0m, \x1B[34m0000000700000007\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000000800000008\x1B[0m, \x1B[34m0000000900000009\x1B[0m, "
      "\x1B[34m0000000a0000000a\x1B[0m, \x1B[34m0000000b0000000b\x1B[0m, "
      "\x1B[34m0000000c0000000c\x1B[0m, \x1B[34m0000000d0000000d\x1B[0m, "
      "\x1B[34m0000000e0000000e\x1B[0m, \x1B[34m0000000f0000000f\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000001000000010\x1B[0m, \x1B[34m0000001100000011\x1B[0m, "
      "\x1B[34m0000001200000012\x1B[0m, \x1B[34m0000001300000013\x1B[0m, "
      "\x1B[34m0000001400000014\x1B[0m, \x1B[34m0000001500000015\x1B[0m, "
      "\x1B[34m0000001600000016\x1B[0m, \x1B[34m0000001700000017\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000001800000018\x1B[0m, \x1B[34m0000001900000019\x1B[0m, "
      "\x1B[34m0000001a0000001a\x1B[0m, \x1B[34m0000001b0000001b\x1B[0m, "
      "\x1B[34m0000001c0000001c\x1B[0m, \x1B[34m0000001d0000001d\x1B[0m, "
      "\x1B[34m0000001e0000001e\x1B[0m, \x1B[34m0000001f0000001f\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000002000000020\x1B[0m, \x1B[34m0000002100000021\x1B[0m, "
      "\x1B[34m0000002200000022\x1B[0m, \x1B[34m0000002300000023\x1B[0m, "
      "\x1B[34m0000002400000024\x1B[0m, \x1B[34m0000002500000025\x1B[0m, "
      "\x1B[34m0000002600000026\x1B[0m, \x1B[34m0000002700000027\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000002800000028\x1B[0m, \x1B[34m0000002900000029\x1B[0m, "
      "\x1B[34m0000002a0000002a\x1B[0m, \x1B[34m0000002b0000002b\x1B[0m, "
      "\x1B[34m0000002c0000002c\x1B[0m, \x1B[34m0000002d0000002d\x1B[0m, "
      "\x1B[34m0000002e0000002e\x1B[0m, \x1B[34m0000002f0000002f\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000003000000030\x1B[0m, \x1B[34m0000003100000031\x1B[0m, "
      "\x1B[34m0000003200000032\x1B[0m, \x1B[34m0000003300000033\x1B[0m, "
      "\x1B[34m0000003400000034\x1B[0m, \x1B[34m0000003500000035\x1B[0m, "
      "\x1B[34m0000003600000036\x1B[0m, \x1B[34m0000003700000037\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000003800000038\x1B[0m, \x1B[34m0000003900000039\x1B[0m, "
      "\x1B[34m0000003a0000003a\x1B[0m, \x1B[34m0000003b0000003b\x1B[0m, "
      "\x1B[34m0000003c0000003c\x1B[0m, \x1B[34m0000003d0000003d\x1B[0m, "
      "\x1B[34m0000003e0000003e\x1B[0m, \x1B[34m0000003f0000003f\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000004000000040\x1B[0m, \x1B[34m0000004100000041\x1B[0m, "
      "\x1B[34m0000004200000042\x1B[0m, \x1B[34m0000004300000043\x1B[0m, "
      "\x1B[34m0000004400000044\x1B[0m, \x1B[34m0000004500000045\x1B[0m, "
      "\x1B[34m0000004600000046\x1B[0m, \x1B[34m0000004700000047\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000004800000048\x1B[0m, \x1B[34m0000004900000049\x1B[0m, "
      "\x1B[34m0000004a0000004a\x1B[0m, \x1B[34m0000004b0000004b\x1B[0m, "
      "\x1B[34m0000004c0000004c\x1B[0m, \x1B[34m0000004d0000004d\x1B[0m, "
      "\x1B[34m0000004e0000004e\x1B[0m, \x1B[34m0000004f0000004f\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000005000000050\x1B[0m, \x1B[34m0000005100000051\x1B[0m, "
      "\x1B[34m0000005200000052\x1B[0m, \x1B[34m0000005300000053\x1B[0m, "
      "\x1B[34m0000005400000054\x1B[0m, \x1B[34m0000005500000055\x1B[0m, "
      "\x1B[34m0000005600000056\x1B[0m, \x1B[34m0000005700000057\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000005800000058\x1B[0m, \x1B[34m0000005900000059\x1B[0m, "
      "\x1B[34m0000005a0000005a\x1B[0m, \x1B[34m0000005b0000005b\x1B[0m, "
      "\x1B[34m0000005c0000005c\x1B[0m, \x1B[34m0000005d0000005d\x1B[0m, "
      "\x1B[34m0000005e0000005e\x1B[0m, \x1B[34m0000005f0000005f\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000006000000060\x1B[0m, \x1B[34m0000006100000061\x1B[0m, "
      "\x1B[34m0000006200000062\x1B[0m, \x1B[34m0000006300000063\x1B[0m, "
      "\x1B[34m0000006400000064\x1B[0m, \x1B[34m0000006500000065\x1B[0m, "
      "\x1B[34m0000006600000066\x1B[0m, \x1B[34m0000006700000067\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000006800000068\x1B[0m, \x1B[34m0000006900000069\x1B[0m, "
      "\x1B[34m0000006a0000006a\x1B[0m, \x1B[34m0000006b0000006b\x1B[0m, "
      "\x1B[34m0000006c0000006c\x1B[0m, \x1B[34m0000006d0000006d\x1B[0m, "
      "\x1B[34m0000006e0000006e\x1B[0m, \x1B[34m0000006f0000006f\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000007000000070\x1B[0m, \x1B[34m0000007100000071\x1B[0m, "
      "\x1B[34m0000007200000072\x1B[0m, \x1B[34m0000007300000073\x1B[0m, "
      "\x1B[34m0000007400000074\x1B[0m, \x1B[34m0000007500000075\x1B[0m, "
      "\x1B[34m0000007600000076\x1B[0m, \x1B[34m0000007700000077\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000007800000078\x1B[0m, \x1B[34m0000007900000079\x1B[0m, "
      "\x1B[34m0000007a0000007a\x1B[0m, \x1B[34m0000007b0000007b\x1B[0m, "
      "\x1B[34m0000007c0000007c\x1B[0m, \x1B[34m0000007d0000007d\x1B[0m, "
      "\x1B[34m0000007e0000007e\x1B[0m, \x1B[34m0000007f0000007f\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000008000000080\x1B[0m, \x1B[34m0000008100000081\x1B[0m, "
      "\x1B[34m0000008200000082\x1B[0m, \x1B[34m0000008300000083\x1B[0m, "
      "\x1B[34m0000008400000084\x1B[0m, \x1B[34m0000008500000085\x1B[0m, "
      "\x1B[34m0000008600000086\x1B[0m, \x1B[34m0000008700000087\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000008800000088\x1B[0m, \x1B[34m0000008900000089\x1B[0m, "
      "\x1B[34m0000008a0000008a\x1B[0m, \x1B[34m0000008b0000008b\x1B[0m, "
      "\x1B[34m0000008c0000008c\x1B[0m, \x1B[34m0000008d0000008d\x1B[0m, "
      "\x1B[34m0000008e0000008e\x1B[0m, \x1B[34m0000008f0000008f\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000009000000090\x1B[0m, \x1B[34m0000009100000091\x1B[0m, "
      "\x1B[34m0000009200000092\x1B[0m, \x1B[34m0000009300000093\x1B[0m, "
      "\x1B[34m0000009400000094\x1B[0m, \x1B[34m0000009500000095\x1B[0m, "
      "\x1B[34m0000009600000096\x1B[0m, \x1B[34m0000009700000097\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000009800000098\x1B[0m, \x1B[34m0000009900000099\x1B[0m, "
      "\x1B[34m0000009a0000009a\x1B[0m, \x1B[34m0000009b0000009b\x1B[0m, "
      "\x1B[34m0000009c0000009c\x1B[0m, \x1B[34m0000009d0000009d\x1B[0m, "
      "\x1B[34m0000009e0000009e\x1B[0m, \x1B[34m0000009f0000009f\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m000000a0000000a0\x1B[0m, \x1B[34m000000a1000000a1\x1B[0m, "
      "\x1B[34m000000a2000000a2\x1B[0m, \x1B[34m000000a3000000a3\x1B[0m, "
      "\x1B[34m000000a4000000a4\x1B[0m, \x1B[34m000000a5000000a5\x1B[0m, "
      "\x1B[34m000000a6000000a6\x1B[0m, \x1B[34m000000a7000000a7\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m000000a8000000a8\x1B[0m, \x1B[34m000000a9000000a9\x1B[0m, "
      "\x1B[34m000000aa000000aa\x1B[0m, \x1B[34m000000ab000000ab\x1B[0m, "
      "\x1B[34m000000ac000000ac\x1B[0m, \x1B[34m000000ad000000ad\x1B[0m, "
      "\x1B[34m000000ae000000ae\x1B[0m, \x1B[34m000000af000000af\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m000000b0000000b0\x1B[0m, \x1B[34m000000b1000000b1\x1B[0m, "
      "\x1B[34m000000b2000000b2\x1B[0m, \x1B[34m000000b3000000b3\x1B[0m, "
      "\x1B[34m000000b4000000b4\x1B[0m, \x1B[34m000000b5000000b5\x1B[0m, "
      "\x1B[34m000000b6000000b6\x1B[0m, \x1B[34m000000b7000000b7\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m000000b8000000b8\x1B[0m, \x1B[34m000000b9000000b9\x1B[0m, "
      "\x1B[34m000000ba000000ba\x1B[0m, \x1B[34m000000bb000000bb\x1B[0m, "
      "\x1B[34m000000bc000000bc\x1B[0m, \x1B[34m000000bd000000bd\x1B[0m, "
      "\x1B[34m000000be000000be\x1B[0m, \x1B[34m000000bf000000bf\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m000000c0000000c0\x1B[0m, \x1B[34m000000c1000000c1\x1B[0m, "
      "\x1B[34m000000c2000000c2\x1B[0m, \x1B[34m000000c3000000c3\x1B[0m, "
      "\x1B[34m000000c4000000c4\x1B[0m, \x1B[34m000000c5000000c5\x1B[0m, "
      "\x1B[34m000000c6000000c6\x1B[0m, \x1B[34m000000c7000000c7\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m000000c8000000c8\x1B[0m, \x1B[34m000000c9000000c9\x1B[0m, "
      "\x1B[34m000000ca000000ca\x1B[0m, \x1B[34m000000cb000000cb\x1B[0m, "
      "\x1B[34m000000cc000000cc\x1B[0m, \x1B[34m000000cd000000cd\x1B[0m, "
      "\x1B[34m000000ce000000ce\x1B[0m, \x1B[34m000000cf000000cf\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m000000d0000000d0\x1B[0m, \x1B[34m000000d1000000d1\x1B[0m, "
      "\x1B[34m000000d2000000d2\x1B[0m, \x1B[34m000000d3000000d3\x1B[0m, "
      "\x1B[34m000000d4000000d4\x1B[0m, \x1B[34m000000d5000000d5\x1B[0m, "
      "\x1B[34m000000d6000000d6\x1B[0m, \x1B[34m000000d7000000d7\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m000000d8000000d8\x1B[0m, \x1B[34m000000d9000000d9\x1B[0m, "
      "\x1B[34m000000da000000da\x1B[0m, \x1B[34m000000db000000db\x1B[0m, "
      "\x1B[34m000000dc000000dc\x1B[0m, \x1B[34m000000dd000000dd\x1B[0m, "
      "\x1B[34m000000de000000de\x1B[0m, \x1B[34m000000df000000df\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m000000e0000000e0\x1B[0m, \x1B[34m000000e1000000e1\x1B[0m, "
      "\x1B[34m000000e2000000e2\x1B[0m, \x1B[34m000000e3000000e3\x1B[0m, "
      "\x1B[34m000000e4000000e4\x1B[0m, \x1B[34m000000e5000000e5\x1B[0m, "
      "\x1B[34m000000e6000000e6\x1B[0m, \x1B[34m000000e7000000e7\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m000000e8000000e8\x1B[0m, \x1B[34m000000e9000000e9\x1B[0m, "
      "\x1B[34m000000ea000000ea\x1B[0m, \x1B[34m000000eb000000eb\x1B[0m, "
      "\x1B[34m000000ec000000ec\x1B[0m, \x1B[34m000000ed000000ed\x1B[0m, "
      "\x1B[34m000000ee000000ee\x1B[0m, \x1B[34m000000ef000000ef\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m000000f0000000f0\x1B[0m, \x1B[34m000000f1000000f1\x1B[0m, "
      "\x1B[34m000000f2000000f2\x1B[0m, \x1B[34m000000f3000000f3\x1B[0m, "
      "\x1B[34m000000f4000000f4\x1B[0m, \x1B[34m000000f5000000f5\x1B[0m, "
      "\x1B[34m000000f6000000f6\x1B[0m, \x1B[34m000000f7000000f7\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m000000f8000000f8\x1B[0m, \x1B[34m000000f9000000f9\x1B[0m, "
      "\x1B[34m000000fa000000fa\x1B[0m, \x1B[34m000000fb000000fb\x1B[0m, "
      "\x1B[34m000000fc000000fc\x1B[0m, \x1B[34m000000fd000000fd\x1B[0m, "
      "\x1B[34m000000fe000000fe\x1B[0m, \x1B[34m000000ff000000ff\x1B[0m\n"
      "          }\n"
      "        }\n"
      "        opmask:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000010000000100\x1B[0m, \x1B[34m0000010100000101\x1B[0m, "
      "\x1B[34m0000010200000102\x1B[0m, \x1B[34m0000010300000103\x1B[0m, "
      "\x1B[34m0000010400000104\x1B[0m, \x1B[34m0000010500000105\x1B[0m, "
      "\x1B[34m0000010600000106\x1B[0m, \x1B[34m0000010700000107\x1B[0m\n"
      "        mxcsr:\x1B[32muint32\x1B[0m: \x1B[34m12345678\x1B[0m\n"
      "      }\n");
}

TEST_F(InterceptionWorkflowTestArm, ZxThreadReadStateDebugRegsAArch64) {
  zx_thread_state_debug_regs_aarch64_t buffer;
  memset(&buffer, 0, sizeof(buffer));
  constexpr uint32_t kCrIncrement = 0x00010001U;
  constexpr uint64_t kVrIncrement = 0x100000001UL;
  uint32_t cr = 0;
  uint64_t vr = 0;
  for (size_t i = 0; i < sizeof(buffer.hw_bps) / sizeof(buffer.hw_bps[0]); ++i) {
    buffer.hw_bps[i].dbgbcr = cr;
    buffer.hw_bps[i].dbgbvr = vr;
    cr += kCrIncrement;
    vr += kVrIncrement;
  }
  buffer.hw_bps_count = 3;
  for (size_t i = 0; i < sizeof(buffer.hw_wps) / sizeof(buffer.hw_wps[0]); ++i) {
    buffer.hw_wps[i].dbgwcr = cr;
    buffer.hw_wps[i].dbgwvr = vr;
    cr += kCrIncrement;
    vr += kVrIncrement;
  }
  buffer.hw_wps_count = 2;
  buffer.esr = 0xeeeeffff;
  THREAD_READ_STATE_DISPLAY_TEST_CONTENT(
      ZX_OK, ZX_THREAD_STATE_DEBUG_REGS, buffer,
      "\n"
      "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
      "zx_thread_read_state("
      "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
      "kind:\x1B[32mzx_thread_state_topic_t\x1B[0m: \x1B[34mZX_THREAD_STATE_DEBUG_REGS\x1B[0m, "
      "buffer_size:\x1B[32msize_t\x1B[0m: \x1B[34m528\x1B[0m)\n"
      "  -> \x1B[32mZX_OK\x1B[0m\n"
      "      regs:\x1B[32mzx_thread_state_debug_regs_aarch64_t\x1B[0m: {\n"
      "        hw_bps:\x1B[32mzx_thread_state_debug_regs_aarch64_bp_t\x1B[0m[]: {\n"
      "          {\n"
      "            dbgbcr:\x1B[32muint32\x1B[0m: \x1B[34m00000000\x1B[0m\n"
      "            dbgbvr:\x1B[32muint64\x1B[0m: \x1B[34m0000000000000000\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            dbgbcr:\x1B[32muint32\x1B[0m: \x1B[34m00010001\x1B[0m\n"
      "            dbgbvr:\x1B[32muint64\x1B[0m: \x1B[34m0000000100000001\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            dbgbcr:\x1B[32muint32\x1B[0m: \x1B[34m00020002\x1B[0m\n"
      "            dbgbvr:\x1B[32muint64\x1B[0m: \x1B[34m0000000200000002\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            dbgbcr:\x1B[32muint32\x1B[0m: \x1B[34m00030003\x1B[0m\n"
      "            dbgbvr:\x1B[32muint64\x1B[0m: \x1B[34m0000000300000003\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            dbgbcr:\x1B[32muint32\x1B[0m: \x1B[34m00040004\x1B[0m\n"
      "            dbgbvr:\x1B[32muint64\x1B[0m: \x1B[34m0000000400000004\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            dbgbcr:\x1B[32muint32\x1B[0m: \x1B[34m00050005\x1B[0m\n"
      "            dbgbvr:\x1B[32muint64\x1B[0m: \x1B[34m0000000500000005\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            dbgbcr:\x1B[32muint32\x1B[0m: \x1B[34m00060006\x1B[0m\n"
      "            dbgbvr:\x1B[32muint64\x1B[0m: \x1B[34m0000000600000006\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            dbgbcr:\x1B[32muint32\x1B[0m: \x1B[34m00070007\x1B[0m\n"
      "            dbgbvr:\x1B[32muint64\x1B[0m: \x1B[34m0000000700000007\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            dbgbcr:\x1B[32muint32\x1B[0m: \x1B[34m00080008\x1B[0m\n"
      "            dbgbvr:\x1B[32muint64\x1B[0m: \x1B[34m0000000800000008\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            dbgbcr:\x1B[32muint32\x1B[0m: \x1B[34m00090009\x1B[0m\n"
      "            dbgbvr:\x1B[32muint64\x1B[0m: \x1B[34m0000000900000009\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            dbgbcr:\x1B[32muint32\x1B[0m: \x1B[34m000a000a\x1B[0m\n"
      "            dbgbvr:\x1B[32muint64\x1B[0m: \x1B[34m0000000a0000000a\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            dbgbcr:\x1B[32muint32\x1B[0m: \x1B[34m000b000b\x1B[0m\n"
      "            dbgbvr:\x1B[32muint64\x1B[0m: \x1B[34m0000000b0000000b\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            dbgbcr:\x1B[32muint32\x1B[0m: \x1B[34m000c000c\x1B[0m\n"
      "            dbgbvr:\x1B[32muint64\x1B[0m: \x1B[34m0000000c0000000c\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            dbgbcr:\x1B[32muint32\x1B[0m: \x1B[34m000d000d\x1B[0m\n"
      "            dbgbvr:\x1B[32muint64\x1B[0m: \x1B[34m0000000d0000000d\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            dbgbcr:\x1B[32muint32\x1B[0m: \x1B[34m000e000e\x1B[0m\n"
      "            dbgbvr:\x1B[32muint64\x1B[0m: \x1B[34m0000000e0000000e\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            dbgbcr:\x1B[32muint32\x1B[0m: \x1B[34m000f000f\x1B[0m\n"
      "            dbgbvr:\x1B[32muint64\x1B[0m: \x1B[34m0000000f0000000f\x1B[0m\n"
      "          }\n"
      "        }\n"
      "        hw_bps_count:\x1B[32muint8\x1B[0m: \x1B[34m03\x1B[0m\n"
      "        hw_wps:\x1B[32mzx_thread_state_debug_regs_aarch64_wp_t\x1B[0m[]: {\n"
      "          {\n"
      "            dbgwcr:\x1B[32muint32\x1B[0m: \x1B[34m00100010\x1B[0m\n"
      "            dbgwvr:\x1B[32muint64\x1B[0m: \x1B[34m0000001000000010\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            dbgwcr:\x1B[32muint32\x1B[0m: \x1B[34m00110011\x1B[0m\n"
      "            dbgwvr:\x1B[32muint64\x1B[0m: \x1B[34m0000001100000011\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            dbgwcr:\x1B[32muint32\x1B[0m: \x1B[34m00120012\x1B[0m\n"
      "            dbgwvr:\x1B[32muint64\x1B[0m: \x1B[34m0000001200000012\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            dbgwcr:\x1B[32muint32\x1B[0m: \x1B[34m00130013\x1B[0m\n"
      "            dbgwvr:\x1B[32muint64\x1B[0m: \x1B[34m0000001300000013\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            dbgwcr:\x1B[32muint32\x1B[0m: \x1B[34m00140014\x1B[0m\n"
      "            dbgwvr:\x1B[32muint64\x1B[0m: \x1B[34m0000001400000014\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            dbgwcr:\x1B[32muint32\x1B[0m: \x1B[34m00150015\x1B[0m\n"
      "            dbgwvr:\x1B[32muint64\x1B[0m: \x1B[34m0000001500000015\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            dbgwcr:\x1B[32muint32\x1B[0m: \x1B[34m00160016\x1B[0m\n"
      "            dbgwvr:\x1B[32muint64\x1B[0m: \x1B[34m0000001600000016\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            dbgwcr:\x1B[32muint32\x1B[0m: \x1B[34m00170017\x1B[0m\n"
      "            dbgwvr:\x1B[32muint64\x1B[0m: \x1B[34m0000001700000017\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            dbgwcr:\x1B[32muint32\x1B[0m: \x1B[34m00180018\x1B[0m\n"
      "            dbgwvr:\x1B[32muint64\x1B[0m: \x1B[34m0000001800000018\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            dbgwcr:\x1B[32muint32\x1B[0m: \x1B[34m00190019\x1B[0m\n"
      "            dbgwvr:\x1B[32muint64\x1B[0m: \x1B[34m0000001900000019\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            dbgwcr:\x1B[32muint32\x1B[0m: \x1B[34m001a001a\x1B[0m\n"
      "            dbgwvr:\x1B[32muint64\x1B[0m: \x1B[34m0000001a0000001a\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            dbgwcr:\x1B[32muint32\x1B[0m: \x1B[34m001b001b\x1B[0m\n"
      "            dbgwvr:\x1B[32muint64\x1B[0m: \x1B[34m0000001b0000001b\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            dbgwcr:\x1B[32muint32\x1B[0m: \x1B[34m001c001c\x1B[0m\n"
      "            dbgwvr:\x1B[32muint64\x1B[0m: \x1B[34m0000001c0000001c\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            dbgwcr:\x1B[32muint32\x1B[0m: \x1B[34m001d001d\x1B[0m\n"
      "            dbgwvr:\x1B[32muint64\x1B[0m: \x1B[34m0000001d0000001d\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            dbgwcr:\x1B[32muint32\x1B[0m: \x1B[34m001e001e\x1B[0m\n"
      "            dbgwvr:\x1B[32muint64\x1B[0m: \x1B[34m0000001e0000001e\x1B[0m\n"
      "          }\n"
      "          {\n"
      "            dbgwcr:\x1B[32muint32\x1B[0m: \x1B[34m001f001f\x1B[0m\n"
      "            dbgwvr:\x1B[32muint64\x1B[0m: \x1B[34m0000001f0000001f\x1B[0m\n"
      "          }\n"
      "        }\n"
      "        hw_wps_count:\x1B[32muint8\x1B[0m: \x1B[34m02\x1B[0m\n"
      "        esr:\x1B[32muint32\x1B[0m: \x1B[34meeeeffff\x1B[0m\n"
      "      }\n");
}

TEST_F(InterceptionWorkflowTestX64, ZxThreadReadStateDebugRegsX64) {
  zx_thread_state_debug_regs_x86_t buffer;
  memset(&buffer, 0, sizeof(buffer));
  constexpr uint64_t kIncrement = 0x100000001UL;
  uint64_t value = 0;
  for (size_t i = 0; i < sizeof(buffer.dr) / sizeof(buffer.dr[0]); ++i) {
    buffer.dr[i] = value;
    value += kIncrement;
  }
  buffer.dr6 = 0x66666666;
  buffer.dr7 = 0x77777777;
  THREAD_READ_STATE_DISPLAY_TEST_CONTENT(
      ZX_OK, ZX_THREAD_STATE_DEBUG_REGS, buffer,
      "\ntest_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
      "zx_thread_read_state("
      "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
      "kind:\x1B[32mzx_thread_state_topic_t\x1B[0m: \x1B[34mZX_THREAD_STATE_DEBUG_REGS\x1B[0m, "
      "buffer_size:\x1B[32msize_t\x1B[0m: \x1B[34m48\x1B[0m)\n"
      "  -> \x1B[32mZX_OK\x1B[0m\n"
      "      regs:\x1B[32mzx_thread_state_debug_regs_x86_t\x1B[0m: {\n"
      "        dr:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000000000000000\x1B[0m, \x1B[34m0000000100000001\x1B[0m, "
      "\x1B[34m0000000200000002\x1B[0m, \x1B[34m0000000300000003\x1B[0m\n"
      "        dr6:\x1B[32muint64\x1B[0m: \x1B[34m0000000066666666\x1B[0m\n"
      "        dr7:\x1B[32muint64\x1B[0m: \x1B[34m0000000077777777\x1B[0m\n"
      "      }\n");
}

#define THREAD_READ_STATE_SINGLE_STEP_DISPLAY_TEST(name, errno, value, expected)            \
  TEST_F(InterceptionWorkflowTestX64, name) {                                               \
    uint32_t single_step = value;                                                           \
    THREAD_READ_STATE_DISPLAY_TEST_CONTENT(errno, ZX_THREAD_STATE_SINGLE_STEP, single_step, \
                                           expected);                                       \
  }                                                                                         \
  TEST_F(InterceptionWorkflowTestArm, name) {                                               \
    uint32_t single_step = value;                                                           \
    THREAD_READ_STATE_DISPLAY_TEST_CONTENT(errno, ZX_THREAD_STATE_SINGLE_STEP, single_step, \
                                           expected);                                       \
  }

THREAD_READ_STATE_SINGLE_STEP_DISPLAY_TEST(
    ZxThreadReadStateSingleStep0, ZX_OK, 0,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_thread_read_state("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "kind:\x1B[32mzx_thread_state_topic_t\x1B[0m: \x1B[34mZX_THREAD_STATE_SINGLE_STEP\x1B[0m, "
    "buffer_size:\x1B[32msize_t\x1B[0m: \x1B[34m4\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (single_step:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n");

THREAD_READ_STATE_SINGLE_STEP_DISPLAY_TEST(
    ZxThreadReadStateSingleStep1, ZX_OK, 1,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_thread_read_state("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "kind:\x1B[32mzx_thread_state_topic_t\x1B[0m: \x1B[34mZX_THREAD_STATE_SINGLE_STEP\x1B[0m, "
    "buffer_size:\x1B[32msize_t\x1B[0m: \x1B[34m4\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (single_step:\x1B[32muint32\x1B[0m: \x1B[34m1\x1B[0m)\n");

TEST_F(InterceptionWorkflowTestX64, ZxThreadReadStateX86RegisterFs) {
  zx_thread_x86_register_fs_t reg = 0x123456789abcdef0UL;
  THREAD_READ_STATE_DISPLAY_TEST_CONTENT(
      ZX_OK, ZX_THREAD_X86_REGISTER_FS, reg,
      "\n"
      "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
      "zx_thread_read_state("
      "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
      "kind:\x1B[32mzx_thread_state_topic_t\x1B[0m: \x1B[34mZX_THREAD_X86_REGISTER_FS\x1B[0m, "
      "buffer_size:\x1B[32msize_t\x1B[0m: \x1B[34m8\x1B[0m)\n"
      "  -> \x1B[32mZX_OK\x1B[0m (reg:\x1B[32muint64\x1B[0m: \x1B[34m123456789abcdef0\x1B[0m)\n");
}

TEST_F(InterceptionWorkflowTestX64, ZxThreadReadStateX86RegisterGs) {
  zx_thread_x86_register_fs_t reg = 0x123456789abcdef0UL;
  THREAD_READ_STATE_DISPLAY_TEST_CONTENT(
      ZX_OK, ZX_THREAD_X86_REGISTER_GS, reg,
      "\n"
      "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
      "zx_thread_read_state("
      "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
      "kind:\x1B[32mzx_thread_state_topic_t\x1B[0m: \x1B[34mZX_THREAD_X86_REGISTER_GS\x1B[0m, "
      "buffer_size:\x1B[32msize_t\x1B[0m: \x1B[34m8\x1B[0m)\n"
      "  -> \x1B[32mZX_OK\x1B[0m (reg:\x1B[32muint64\x1B[0m: \x1B[34m123456789abcdef0\x1B[0m)\n");
}

// zx_thread_write_state tests.

std::unique_ptr<SystemCallTest> ZxThreadWriteState(int64_t result, std::string_view result_name,
                                                   zx_handle_t handle, uint32_t kind, void* buffer,
                                                   size_t buffer_size) {
  auto value = std::make_unique<SystemCallTest>("zx_thread_write_state", result, result_name);
  value->AddInput(handle);
  value->AddInput(kind);
  value->AddInput(reinterpret_cast<uint64_t>(buffer));
  value->AddInput(buffer_size);
  return value;
}

#define THREAD_WRITE_STATE_DISPLAY_TEST_CONTENT(result, kind, buffer, expected)                   \
  PerformDisplayTest("$plt(zx_thread_write_state)",                                               \
                     ZxThreadWriteState(result, #result, kHandle, kind, &buffer, sizeof(buffer)), \
                     expected);

TEST_F(InterceptionWorkflowTestArm, ZxThreadWriteStateGeneralRegsAArch64) {
  zx_thread_state_general_regs_aarch64_t buffer;
  memset(&buffer, 0, sizeof(buffer));
  constexpr uint64_t kIncrement = 0x100000001UL;
  uint64_t value = 0;
  for (size_t i = 0; i < sizeof(buffer.r) / sizeof(buffer.r[0]); ++i) {
    buffer.r[i] = value;
    value += kIncrement;
  }
  buffer.lr = 0x11111111;
  buffer.sp = 0x22222222;
  buffer.pc = 0xcccccccc;
  buffer.cpsr = 0xdddddddd;
  buffer.tpidr = 0xeeeeeeee;
  THREAD_WRITE_STATE_DISPLAY_TEST_CONTENT(
      ZX_OK, ZX_THREAD_STATE_GENERAL_REGS, buffer,
      "\n"
      "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
      "zx_thread_write_state("
      "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
      "kind:\x1B[32mzx_thread_state_topic_t\x1B[0m: \x1B[34mZX_THREAD_STATE_GENERAL_REGS\x1B[0m, "
      "buffer_size:\x1B[32msize_t\x1B[0m: \x1B[34m280\x1B[0m)\n"
      "    regs:\x1B[32mzx_thread_state_general_regs_aarch64_t\x1B[0m: {\n"
      "      r:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000000000000000\x1B[0m, \x1B[34m0000000100000001\x1B[0m, "
      "\x1B[34m0000000200000002\x1B[0m, \x1B[34m0000000300000003\x1B[0m, "
      "\x1B[34m0000000400000004\x1B[0m, \x1B[34m0000000500000005\x1B[0m, "
      "\x1B[34m0000000600000006\x1B[0m, \x1B[34m0000000700000007\x1B[0m, "
      "\x1B[34m0000000800000008\x1B[0m, \x1B[34m0000000900000009\x1B[0m, "
      "\x1B[34m0000000a0000000a\x1B[0m, \x1B[34m0000000b0000000b\x1B[0m, "
      "\x1B[34m0000000c0000000c\x1B[0m, \x1B[34m0000000d0000000d\x1B[0m, "
      "\x1B[34m0000000e0000000e\x1B[0m, \x1B[34m0000000f0000000f\x1B[0m, "
      "\x1B[34m0000001000000010\x1B[0m, \x1B[34m0000001100000011\x1B[0m, "
      "\x1B[34m0000001200000012\x1B[0m, \x1B[34m0000001300000013\x1B[0m, "
      "\x1B[34m0000001400000014\x1B[0m, \x1B[34m0000001500000015\x1B[0m, "
      "\x1B[34m0000001600000016\x1B[0m, \x1B[34m0000001700000017\x1B[0m, "
      "\x1B[34m0000001800000018\x1B[0m, \x1B[34m0000001900000019\x1B[0m, "
      "\x1B[34m0000001a0000001a\x1B[0m, \x1B[34m0000001b0000001b\x1B[0m, "
      "\x1B[34m0000001c0000001c\x1B[0m, \x1B[34m0000001d0000001d\x1B[0m\n"
      "      lr:\x1B[32muint64\x1B[0m: \x1B[34m0000000011111111\x1B[0m\n"
      "      sp:\x1B[32muint64\x1B[0m: \x1B[34m0000000022222222\x1B[0m\n"
      "      pc:\x1B[32muint64\x1B[0m: \x1B[34m00000000cccccccc\x1B[0m\n"
      "      cpsr:\x1B[32muint64\x1B[0m: \x1B[34m00000000dddddddd\x1B[0m\n"
      "      tpidr:\x1B[32muint64\x1B[0m: \x1B[34m00000000eeeeeeee\x1B[0m\n"
      "    }\n"
      "  -> \x1B[32mZX_OK\x1B[0m\n");
}

TEST_F(InterceptionWorkflowTestX64, ZxThreadWriteStateGeneralRegsX64) {
  zx_thread_state_general_regs_x86_t buffer;
  memset(&buffer, 0, sizeof(buffer));
  buffer.rax = 0xaaaa;
  buffer.rbx = 0xbbbb;
  buffer.rcx = 0xcccc;
  buffer.rdx = 0xdddd;
  buffer.rsi = 0x1234;
  buffer.rbp = 0x2345;
  buffer.rsp = 0x3456;
  buffer.r8 = 0x0808;
  buffer.r9 = 0x0909;
  buffer.r10 = 0x1010;
  buffer.r11 = 0x1111;
  buffer.r12 = 0x1212;
  buffer.r13 = 0x1313;
  buffer.r14 = 0x1414;
  buffer.r15 = 0x1515;
  buffer.rip = 0x1111;
  buffer.rflags = 0;
  buffer.fs_base = 0x100000000UL;
  buffer.gs_base = 0x200000000UL;
  THREAD_WRITE_STATE_DISPLAY_TEST_CONTENT(
      ZX_OK, ZX_THREAD_STATE_GENERAL_REGS, buffer,
      "\n"
      "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
      "zx_thread_write_state("
      "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
      "kind:\x1B[32mzx_thread_state_topic_t\x1B[0m: \x1B[34mZX_THREAD_STATE_GENERAL_REGS\x1B[0m, "
      "buffer_size:\x1B[32msize_t\x1B[0m: \x1B[34m160\x1B[0m)\n"
      "    regs:\x1B[32mzx_thread_state_general_regs_x86_t\x1B[0m: {\n"
      "      rax:\x1B[32muint64\x1B[0m: \x1B[34m000000000000aaaa\x1B[0m\n"
      "      rbx:\x1B[32muint64\x1B[0m: \x1B[34m000000000000bbbb\x1B[0m\n"
      "      rcx:\x1B[32muint64\x1B[0m: \x1B[34m000000000000cccc\x1B[0m\n"
      "      rdx:\x1B[32muint64\x1B[0m: \x1B[34m000000000000dddd\x1B[0m\n"
      "      rsi:\x1B[32muint64\x1B[0m: \x1B[34m0000000000001234\x1B[0m\n"
      "      rdi:\x1B[32muint64\x1B[0m: \x1B[34m0000000000000000\x1B[0m\n"
      "      rbp:\x1B[32muint64\x1B[0m: \x1B[34m0000000000002345\x1B[0m\n"
      "      rsp:\x1B[32muint64\x1B[0m: \x1B[34m0000000000003456\x1B[0m\n"
      "      r8:\x1B[32muint64\x1B[0m: \x1B[34m0000000000000808\x1B[0m\n"
      "      r9:\x1B[32muint64\x1B[0m: \x1B[34m0000000000000909\x1B[0m\n"
      "      r10:\x1B[32muint64\x1B[0m: \x1B[34m0000000000001010\x1B[0m\n"
      "      r11:\x1B[32muint64\x1B[0m: \x1B[34m0000000000001111\x1B[0m\n"
      "      r12:\x1B[32muint64\x1B[0m: \x1B[34m0000000000001212\x1B[0m\n"
      "      r13:\x1B[32muint64\x1B[0m: \x1B[34m0000000000001313\x1B[0m\n"
      "      r14:\x1B[32muint64\x1B[0m: \x1B[34m0000000000001414\x1B[0m\n"
      "      r15:\x1B[32muint64\x1B[0m: \x1B[34m0000000000001515\x1B[0m\n"
      "      rip:\x1B[32muint64\x1B[0m: \x1B[34m0000000000001111\x1B[0m\n"
      "      rflags:\x1B[32muint64\x1B[0m: \x1B[34m0000000000000000\x1B[0m\n"
      "      fs_base:\x1B[32muint64\x1B[0m: \x1B[34m0000000100000000\x1B[0m\n"
      "      gs_base:\x1B[32muint64\x1B[0m: \x1B[34m0000000200000000\x1B[0m\n"
      "    }\n"
      "  -> \x1B[32mZX_OK\x1B[0m\n");
}

TEST_F(InterceptionWorkflowTestX64, ZxThreadWriteStateFpRegsX64) {
  zx_thread_state_fp_regs_x86_t buffer;
  memset(&buffer, 0, sizeof(buffer));
  buffer.fcw = 0xcccc;
  buffer.fsw = 0xdddd;
  buffer.ftw = 0xee;
  buffer.fop = 0xffff;
  buffer.fip = 0x100000001UL;
  buffer.fdp = 0xd0000000dUL;
  constexpr uint64_t kLowIncrement = 0x100000001UL;
  constexpr uint64_t kHighIncrement = 0x100000000UL;
  uint64_t low = 0;
  uint64_t high = 0;
  for (size_t i = 0; i < sizeof(buffer.st) / sizeof(buffer.st[0]); ++i) {
    buffer.st[i].low = low;
    buffer.st[i].high = high;
    low += kLowIncrement;
    high += kHighIncrement;
  }
  THREAD_WRITE_STATE_DISPLAY_TEST_CONTENT(
      ZX_OK, ZX_THREAD_STATE_FP_REGS, buffer,
      "\n"
      "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
      "zx_thread_write_state("
      "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
      "kind:\x1B[32mzx_thread_state_topic_t\x1B[0m: \x1B[34mZX_THREAD_STATE_FP_REGS\x1B[0m, "
      "buffer_size:\x1B[32msize_t\x1B[0m: \x1B[34m160\x1B[0m)\n"
      "    regs:\x1B[32mzx_thread_state_fp_regs_x86_t\x1B[0m: {\n"
      "      fcw:\x1B[32muint16\x1B[0m: \x1B[34mcccc\x1B[0m\n"
      "      fsw:\x1B[32muint16\x1B[0m: \x1B[34mdddd\x1B[0m\n"
      "      ftw:\x1B[32muint8\x1B[0m: \x1B[34mee\x1B[0m\n"
      "      fop:\x1B[32muint16\x1B[0m: \x1B[34mffff\x1B[0m\n"
      "      fip:\x1B[32muint64\x1B[0m: \x1B[34m0000000100000001\x1B[0m\n"
      "      fdp:\x1B[32muint64\x1B[0m: \x1B[34m0000000d0000000d\x1B[0m\n"
      "      st:\x1B[32muint128[]\x1B[0m: "
      "\x1B[34m{ low = 0000000000000000, high = 0000000000000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000100000001, high = 0000000100000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000200000002, high = 0000000200000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000300000003, high = 0000000300000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000400000004, high = 0000000400000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000500000005, high = 0000000500000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000600000006, high = 0000000600000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000700000007, high = 0000000700000000 }\x1B[0m\n"
      "    }\n"
      "  -> \x1B[32mZX_OK\x1B[0m\n");
}

TEST_F(InterceptionWorkflowTestArm, ZxThreadWriteStateVectorRegsAArch64) {
  zx_thread_state_vector_regs_aarch64_t buffer;
  memset(&buffer, 0, sizeof(buffer));
  buffer.fpcr = 0x12345678U;
  buffer.fpsr = 0x87654321U;
  constexpr uint64_t kLowIncrement = 0x100000001UL;
  constexpr uint64_t kHighIncrement = 0x100000000UL;
  uint64_t low = 0;
  uint64_t high = 0;
  for (size_t i = 0; i < sizeof(buffer.v) / sizeof(buffer.v[0]); ++i) {
    buffer.v[i].low = low;
    buffer.v[i].high = high;
    low += kLowIncrement;
    high += kHighIncrement;
  }
  THREAD_WRITE_STATE_DISPLAY_TEST_CONTENT(
      ZX_OK, ZX_THREAD_STATE_VECTOR_REGS, buffer,
      "\n"
      "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
      "zx_thread_write_state("
      "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
      "kind:\x1B[32mzx_thread_state_topic_t\x1B[0m: \x1B[34mZX_THREAD_STATE_VECTOR_REGS\x1B[0m, "
      "buffer_size:\x1B[32msize_t\x1B[0m: \x1B[34m520\x1B[0m)\n"
      "    regs:\x1B[32mzx_thread_state_vector_regs_aarch64_t\x1B[0m: {\n"
      "      fpcr:\x1B[32muint32\x1B[0m: \x1B[34m12345678\x1B[0m\n"
      "      fpsr:\x1B[32muint32\x1B[0m: \x1B[34m87654321\x1B[0m\n"
      "      v:\x1B[32muint128[]\x1B[0m: "
      "\x1B[34m{ low = 0000000000000000, high = 0000000000000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000100000001, high = 0000000100000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000200000002, high = 0000000200000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000300000003, high = 0000000300000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000400000004, high = 0000000400000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000500000005, high = 0000000500000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000600000006, high = 0000000600000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000700000007, high = 0000000700000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000800000008, high = 0000000800000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000900000009, high = 0000000900000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000a0000000a, high = 0000000a00000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000b0000000b, high = 0000000b00000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000c0000000c, high = 0000000c00000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000d0000000d, high = 0000000d00000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000e0000000e, high = 0000000e00000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000000f0000000f, high = 0000000f00000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000001000000010, high = 0000001000000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000001100000011, high = 0000001100000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000001200000012, high = 0000001200000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000001300000013, high = 0000001300000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000001400000014, high = 0000001400000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000001500000015, high = 0000001500000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000001600000016, high = 0000001600000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000001700000017, high = 0000001700000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000001800000018, high = 0000001800000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000001900000019, high = 0000001900000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000001a0000001a, high = 0000001a00000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000001b0000001b, high = 0000001b00000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000001c0000001c, high = 0000001c00000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000001d0000001d, high = 0000001d00000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000001e0000001e, high = 0000001e00000000 }\x1B[0m, "
      "\x1B[34m{ low = 0000001f0000001f, high = 0000001f00000000 }\x1B[0m\n"
      "    }\n"
      "  -> \x1B[32mZX_OK\x1B[0m\n");
}

TEST_F(InterceptionWorkflowTestX64, ZxThreadWriteStateVectorRegsX64) {
  zx_thread_state_vector_regs_x86_t buffer;
  memset(&buffer, 0, sizeof(buffer));
  constexpr uint64_t kIncrement = 0x100000001UL;
  uint64_t value = 0;
  for (size_t i = 0; i < sizeof(buffer.zmm) / sizeof(buffer.zmm[0]); ++i) {
    for (size_t j = 0; j < sizeof(buffer.zmm[0].v) / sizeof(buffer.zmm[0].v[0]); ++j) {
      buffer.zmm[i].v[j] = value;
      value += kIncrement;
    }
  }
  for (size_t i = 0; i < sizeof(buffer.opmask) / sizeof(buffer.opmask[0]); ++i) {
    buffer.opmask[i] = value;
    value += kIncrement;
  }
  buffer.mxcsr = 0x12345678;
  THREAD_WRITE_STATE_DISPLAY_TEST_CONTENT(
      ZX_OK, ZX_THREAD_STATE_VECTOR_REGS, buffer,
      "\n"
      "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
      "zx_thread_write_state("
      "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
      "kind:\x1B[32mzx_thread_state_topic_t\x1B[0m: \x1B[34mZX_THREAD_STATE_VECTOR_REGS\x1B[0m, "
      "buffer_size:\x1B[32msize_t\x1B[0m: \x1B[34m2120\x1B[0m)\n"
      "    regs:\x1B[32mzx_thread_state_vector_regs_x86_t\x1B[0m: {\n"
      "      zmm:\x1B[32mzx_thread_state_vector_regs_x86_zmm_t\x1B[0m[]: {\n"
      "        {\n"
      "          v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000000000000000\x1B[0m, \x1B[34m0000000100000001\x1B[0m, "
      "\x1B[34m0000000200000002\x1B[0m, \x1B[34m0000000300000003\x1B[0m, "
      "\x1B[34m0000000400000004\x1B[0m, \x1B[34m0000000500000005\x1B[0m, "
      "\x1B[34m0000000600000006\x1B[0m, \x1B[34m0000000700000007\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000000800000008\x1B[0m, \x1B[34m0000000900000009\x1B[0m, "
      "\x1B[34m0000000a0000000a\x1B[0m, \x1B[34m0000000b0000000b\x1B[0m, "
      "\x1B[34m0000000c0000000c\x1B[0m, \x1B[34m0000000d0000000d\x1B[0m, "
      "\x1B[34m0000000e0000000e\x1B[0m, \x1B[34m0000000f0000000f\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000001000000010\x1B[0m, \x1B[34m0000001100000011\x1B[0m, "
      "\x1B[34m0000001200000012\x1B[0m, \x1B[34m0000001300000013\x1B[0m, "
      "\x1B[34m0000001400000014\x1B[0m, \x1B[34m0000001500000015\x1B[0m, "
      "\x1B[34m0000001600000016\x1B[0m, \x1B[34m0000001700000017\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000001800000018\x1B[0m, \x1B[34m0000001900000019\x1B[0m, "
      "\x1B[34m0000001a0000001a\x1B[0m, \x1B[34m0000001b0000001b\x1B[0m, "
      "\x1B[34m0000001c0000001c\x1B[0m, \x1B[34m0000001d0000001d\x1B[0m, "
      "\x1B[34m0000001e0000001e\x1B[0m, \x1B[34m0000001f0000001f\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000002000000020\x1B[0m, \x1B[34m0000002100000021\x1B[0m, "
      "\x1B[34m0000002200000022\x1B[0m, \x1B[34m0000002300000023\x1B[0m, "
      "\x1B[34m0000002400000024\x1B[0m, \x1B[34m0000002500000025\x1B[0m, "
      "\x1B[34m0000002600000026\x1B[0m, \x1B[34m0000002700000027\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000002800000028\x1B[0m, \x1B[34m0000002900000029\x1B[0m, "
      "\x1B[34m0000002a0000002a\x1B[0m, \x1B[34m0000002b0000002b\x1B[0m, "
      "\x1B[34m0000002c0000002c\x1B[0m, \x1B[34m0000002d0000002d\x1B[0m, "
      "\x1B[34m0000002e0000002e\x1B[0m, \x1B[34m0000002f0000002f\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000003000000030\x1B[0m, \x1B[34m0000003100000031\x1B[0m, "
      "\x1B[34m0000003200000032\x1B[0m, \x1B[34m0000003300000033\x1B[0m, "
      "\x1B[34m0000003400000034\x1B[0m, \x1B[34m0000003500000035\x1B[0m, "
      "\x1B[34m0000003600000036\x1B[0m, \x1B[34m0000003700000037\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000003800000038\x1B[0m, \x1B[34m0000003900000039\x1B[0m, "
      "\x1B[34m0000003a0000003a\x1B[0m, \x1B[34m0000003b0000003b\x1B[0m, "
      "\x1B[34m0000003c0000003c\x1B[0m, \x1B[34m0000003d0000003d\x1B[0m, "
      "\x1B[34m0000003e0000003e\x1B[0m, \x1B[34m0000003f0000003f\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000004000000040\x1B[0m, \x1B[34m0000004100000041\x1B[0m, "
      "\x1B[34m0000004200000042\x1B[0m, \x1B[34m0000004300000043\x1B[0m, "
      "\x1B[34m0000004400000044\x1B[0m, \x1B[34m0000004500000045\x1B[0m, "
      "\x1B[34m0000004600000046\x1B[0m, \x1B[34m0000004700000047\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000004800000048\x1B[0m, \x1B[34m0000004900000049\x1B[0m, "
      "\x1B[34m0000004a0000004a\x1B[0m, \x1B[34m0000004b0000004b\x1B[0m, "
      "\x1B[34m0000004c0000004c\x1B[0m, \x1B[34m0000004d0000004d\x1B[0m, "
      "\x1B[34m0000004e0000004e\x1B[0m, \x1B[34m0000004f0000004f\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000005000000050\x1B[0m, \x1B[34m0000005100000051\x1B[0m, "
      "\x1B[34m0000005200000052\x1B[0m, \x1B[34m0000005300000053\x1B[0m, "
      "\x1B[34m0000005400000054\x1B[0m, \x1B[34m0000005500000055\x1B[0m, "
      "\x1B[34m0000005600000056\x1B[0m, \x1B[34m0000005700000057\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000005800000058\x1B[0m, \x1B[34m0000005900000059\x1B[0m, "
      "\x1B[34m0000005a0000005a\x1B[0m, \x1B[34m0000005b0000005b\x1B[0m, "
      "\x1B[34m0000005c0000005c\x1B[0m, \x1B[34m0000005d0000005d\x1B[0m, "
      "\x1B[34m0000005e0000005e\x1B[0m, \x1B[34m0000005f0000005f\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000006000000060\x1B[0m, \x1B[34m0000006100000061\x1B[0m, "
      "\x1B[34m0000006200000062\x1B[0m, \x1B[34m0000006300000063\x1B[0m, "
      "\x1B[34m0000006400000064\x1B[0m, \x1B[34m0000006500000065\x1B[0m, "
      "\x1B[34m0000006600000066\x1B[0m, \x1B[34m0000006700000067\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000006800000068\x1B[0m, \x1B[34m0000006900000069\x1B[0m, "
      "\x1B[34m0000006a0000006a\x1B[0m, \x1B[34m0000006b0000006b\x1B[0m, "
      "\x1B[34m0000006c0000006c\x1B[0m, \x1B[34m0000006d0000006d\x1B[0m, "
      "\x1B[34m0000006e0000006e\x1B[0m, \x1B[34m0000006f0000006f\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000007000000070\x1B[0m, \x1B[34m0000007100000071\x1B[0m, "
      "\x1B[34m0000007200000072\x1B[0m, \x1B[34m0000007300000073\x1B[0m, "
      "\x1B[34m0000007400000074\x1B[0m, \x1B[34m0000007500000075\x1B[0m, "
      "\x1B[34m0000007600000076\x1B[0m, \x1B[34m0000007700000077\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000007800000078\x1B[0m, \x1B[34m0000007900000079\x1B[0m, "
      "\x1B[34m0000007a0000007a\x1B[0m, \x1B[34m0000007b0000007b\x1B[0m, "
      "\x1B[34m0000007c0000007c\x1B[0m, \x1B[34m0000007d0000007d\x1B[0m, "
      "\x1B[34m0000007e0000007e\x1B[0m, \x1B[34m0000007f0000007f\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000008000000080\x1B[0m, \x1B[34m0000008100000081\x1B[0m, "
      "\x1B[34m0000008200000082\x1B[0m, \x1B[34m0000008300000083\x1B[0m, "
      "\x1B[34m0000008400000084\x1B[0m, \x1B[34m0000008500000085\x1B[0m, "
      "\x1B[34m0000008600000086\x1B[0m, \x1B[34m0000008700000087\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000008800000088\x1B[0m, \x1B[34m0000008900000089\x1B[0m, "
      "\x1B[34m0000008a0000008a\x1B[0m, \x1B[34m0000008b0000008b\x1B[0m, "
      "\x1B[34m0000008c0000008c\x1B[0m, \x1B[34m0000008d0000008d\x1B[0m, "
      "\x1B[34m0000008e0000008e\x1B[0m, \x1B[34m0000008f0000008f\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000009000000090\x1B[0m, \x1B[34m0000009100000091\x1B[0m, "
      "\x1B[34m0000009200000092\x1B[0m, \x1B[34m0000009300000093\x1B[0m, "
      "\x1B[34m0000009400000094\x1B[0m, \x1B[34m0000009500000095\x1B[0m, "
      "\x1B[34m0000009600000096\x1B[0m, \x1B[34m0000009700000097\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000009800000098\x1B[0m, \x1B[34m0000009900000099\x1B[0m, "
      "\x1B[34m0000009a0000009a\x1B[0m, \x1B[34m0000009b0000009b\x1B[0m, "
      "\x1B[34m0000009c0000009c\x1B[0m, \x1B[34m0000009d0000009d\x1B[0m, "
      "\x1B[34m0000009e0000009e\x1B[0m, \x1B[34m0000009f0000009f\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m000000a0000000a0\x1B[0m, \x1B[34m000000a1000000a1\x1B[0m, "
      "\x1B[34m000000a2000000a2\x1B[0m, \x1B[34m000000a3000000a3\x1B[0m, "
      "\x1B[34m000000a4000000a4\x1B[0m, \x1B[34m000000a5000000a5\x1B[0m, "
      "\x1B[34m000000a6000000a6\x1B[0m, \x1B[34m000000a7000000a7\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m000000a8000000a8\x1B[0m, \x1B[34m000000a9000000a9\x1B[0m, "
      "\x1B[34m000000aa000000aa\x1B[0m, \x1B[34m000000ab000000ab\x1B[0m, "
      "\x1B[34m000000ac000000ac\x1B[0m, \x1B[34m000000ad000000ad\x1B[0m, "
      "\x1B[34m000000ae000000ae\x1B[0m, \x1B[34m000000af000000af\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m000000b0000000b0\x1B[0m, \x1B[34m000000b1000000b1\x1B[0m, "
      "\x1B[34m000000b2000000b2\x1B[0m, \x1B[34m000000b3000000b3\x1B[0m, "
      "\x1B[34m000000b4000000b4\x1B[0m, \x1B[34m000000b5000000b5\x1B[0m, "
      "\x1B[34m000000b6000000b6\x1B[0m, \x1B[34m000000b7000000b7\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m000000b8000000b8\x1B[0m, \x1B[34m000000b9000000b9\x1B[0m, "
      "\x1B[34m000000ba000000ba\x1B[0m, \x1B[34m000000bb000000bb\x1B[0m, "
      "\x1B[34m000000bc000000bc\x1B[0m, \x1B[34m000000bd000000bd\x1B[0m, "
      "\x1B[34m000000be000000be\x1B[0m, \x1B[34m000000bf000000bf\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m000000c0000000c0\x1B[0m, \x1B[34m000000c1000000c1\x1B[0m, "
      "\x1B[34m000000c2000000c2\x1B[0m, \x1B[34m000000c3000000c3\x1B[0m, "
      "\x1B[34m000000c4000000c4\x1B[0m, \x1B[34m000000c5000000c5\x1B[0m, "
      "\x1B[34m000000c6000000c6\x1B[0m, \x1B[34m000000c7000000c7\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m000000c8000000c8\x1B[0m, \x1B[34m000000c9000000c9\x1B[0m, "
      "\x1B[34m000000ca000000ca\x1B[0m, \x1B[34m000000cb000000cb\x1B[0m, "
      "\x1B[34m000000cc000000cc\x1B[0m, \x1B[34m000000cd000000cd\x1B[0m, "
      "\x1B[34m000000ce000000ce\x1B[0m, \x1B[34m000000cf000000cf\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m000000d0000000d0\x1B[0m, \x1B[34m000000d1000000d1\x1B[0m, "
      "\x1B[34m000000d2000000d2\x1B[0m, \x1B[34m000000d3000000d3\x1B[0m, "
      "\x1B[34m000000d4000000d4\x1B[0m, \x1B[34m000000d5000000d5\x1B[0m, "
      "\x1B[34m000000d6000000d6\x1B[0m, \x1B[34m000000d7000000d7\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m000000d8000000d8\x1B[0m, \x1B[34m000000d9000000d9\x1B[0m, "
      "\x1B[34m000000da000000da\x1B[0m, \x1B[34m000000db000000db\x1B[0m, "
      "\x1B[34m000000dc000000dc\x1B[0m, \x1B[34m000000dd000000dd\x1B[0m, "
      "\x1B[34m000000de000000de\x1B[0m, \x1B[34m000000df000000df\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m000000e0000000e0\x1B[0m, \x1B[34m000000e1000000e1\x1B[0m, "
      "\x1B[34m000000e2000000e2\x1B[0m, \x1B[34m000000e3000000e3\x1B[0m, "
      "\x1B[34m000000e4000000e4\x1B[0m, \x1B[34m000000e5000000e5\x1B[0m, "
      "\x1B[34m000000e6000000e6\x1B[0m, \x1B[34m000000e7000000e7\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m000000e8000000e8\x1B[0m, \x1B[34m000000e9000000e9\x1B[0m, "
      "\x1B[34m000000ea000000ea\x1B[0m, \x1B[34m000000eb000000eb\x1B[0m, "
      "\x1B[34m000000ec000000ec\x1B[0m, \x1B[34m000000ed000000ed\x1B[0m, "
      "\x1B[34m000000ee000000ee\x1B[0m, \x1B[34m000000ef000000ef\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m000000f0000000f0\x1B[0m, \x1B[34m000000f1000000f1\x1B[0m, "
      "\x1B[34m000000f2000000f2\x1B[0m, \x1B[34m000000f3000000f3\x1B[0m, "
      "\x1B[34m000000f4000000f4\x1B[0m, \x1B[34m000000f5000000f5\x1B[0m, "
      "\x1B[34m000000f6000000f6\x1B[0m, \x1B[34m000000f7000000f7\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          v:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m000000f8000000f8\x1B[0m, \x1B[34m000000f9000000f9\x1B[0m, "
      "\x1B[34m000000fa000000fa\x1B[0m, \x1B[34m000000fb000000fb\x1B[0m, "
      "\x1B[34m000000fc000000fc\x1B[0m, \x1B[34m000000fd000000fd\x1B[0m, "
      "\x1B[34m000000fe000000fe\x1B[0m, \x1B[34m000000ff000000ff\x1B[0m\n"
      "        }\n"
      "      }\n"
      "      opmask:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000010000000100\x1B[0m, \x1B[34m0000010100000101\x1B[0m, "
      "\x1B[34m0000010200000102\x1B[0m, \x1B[34m0000010300000103\x1B[0m, "
      "\x1B[34m0000010400000104\x1B[0m, \x1B[34m0000010500000105\x1B[0m, "
      "\x1B[34m0000010600000106\x1B[0m, \x1B[34m0000010700000107\x1B[0m\n"
      "      mxcsr:\x1B[32muint32\x1B[0m: \x1B[34m12345678\x1B[0m\n"
      "    }\n"
      "  -> \x1B[32mZX_OK\x1B[0m\n");
}

TEST_F(InterceptionWorkflowTestArm, ZxThreadWriteStateDebugRegsAArch64) {
  zx_thread_state_debug_regs_aarch64_t buffer;
  memset(&buffer, 0, sizeof(buffer));
  constexpr uint32_t kCrIncrement = 0x00010001U;
  constexpr uint64_t kVrIncrement = 0x100000001UL;
  uint32_t cr = 0;
  uint64_t vr = 0;
  for (size_t i = 0; i < sizeof(buffer.hw_bps) / sizeof(buffer.hw_bps[0]); ++i) {
    buffer.hw_bps[i].dbgbcr = cr;
    buffer.hw_bps[i].dbgbvr = vr;
    cr += kCrIncrement;
    vr += kVrIncrement;
  }
  buffer.hw_bps_count = 3;
  for (size_t i = 0; i < sizeof(buffer.hw_wps) / sizeof(buffer.hw_wps[0]); ++i) {
    buffer.hw_wps[i].dbgwcr = cr;
    buffer.hw_wps[i].dbgwvr = vr;
    cr += kCrIncrement;
    vr += kVrIncrement;
  }
  buffer.hw_wps_count = 2;
  buffer.esr = 0xeeeeffff;
  THREAD_WRITE_STATE_DISPLAY_TEST_CONTENT(
      ZX_OK, ZX_THREAD_STATE_DEBUG_REGS, buffer,
      "\n"
      "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
      "zx_thread_write_state("
      "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
      "kind:\x1B[32mzx_thread_state_topic_t\x1B[0m: \x1B[34mZX_THREAD_STATE_DEBUG_REGS\x1B[0m, "
      "buffer_size:\x1B[32msize_t\x1B[0m: \x1B[34m528\x1B[0m)\n"
      "    regs:\x1B[32mzx_thread_state_debug_regs_aarch64_t\x1B[0m: {\n"
      "      hw_bps:\x1B[32mzx_thread_state_debug_regs_aarch64_bp_t\x1B[0m[]: {\n"
      "        {\n"
      "          dbgbcr:\x1B[32muint32\x1B[0m: \x1B[34m00000000\x1B[0m\n"
      "          dbgbvr:\x1B[32muint64\x1B[0m: \x1B[34m0000000000000000\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          dbgbcr:\x1B[32muint32\x1B[0m: \x1B[34m00010001\x1B[0m\n"
      "          dbgbvr:\x1B[32muint64\x1B[0m: \x1B[34m0000000100000001\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          dbgbcr:\x1B[32muint32\x1B[0m: \x1B[34m00020002\x1B[0m\n"
      "          dbgbvr:\x1B[32muint64\x1B[0m: \x1B[34m0000000200000002\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          dbgbcr:\x1B[32muint32\x1B[0m: \x1B[34m00030003\x1B[0m\n"
      "          dbgbvr:\x1B[32muint64\x1B[0m: \x1B[34m0000000300000003\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          dbgbcr:\x1B[32muint32\x1B[0m: \x1B[34m00040004\x1B[0m\n"
      "          dbgbvr:\x1B[32muint64\x1B[0m: \x1B[34m0000000400000004\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          dbgbcr:\x1B[32muint32\x1B[0m: \x1B[34m00050005\x1B[0m\n"
      "          dbgbvr:\x1B[32muint64\x1B[0m: \x1B[34m0000000500000005\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          dbgbcr:\x1B[32muint32\x1B[0m: \x1B[34m00060006\x1B[0m\n"
      "          dbgbvr:\x1B[32muint64\x1B[0m: \x1B[34m0000000600000006\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          dbgbcr:\x1B[32muint32\x1B[0m: \x1B[34m00070007\x1B[0m\n"
      "          dbgbvr:\x1B[32muint64\x1B[0m: \x1B[34m0000000700000007\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          dbgbcr:\x1B[32muint32\x1B[0m: \x1B[34m00080008\x1B[0m\n"
      "          dbgbvr:\x1B[32muint64\x1B[0m: \x1B[34m0000000800000008\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          dbgbcr:\x1B[32muint32\x1B[0m: \x1B[34m00090009\x1B[0m\n"
      "          dbgbvr:\x1B[32muint64\x1B[0m: \x1B[34m0000000900000009\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          dbgbcr:\x1B[32muint32\x1B[0m: \x1B[34m000a000a\x1B[0m\n"
      "          dbgbvr:\x1B[32muint64\x1B[0m: \x1B[34m0000000a0000000a\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          dbgbcr:\x1B[32muint32\x1B[0m: \x1B[34m000b000b\x1B[0m\n"
      "          dbgbvr:\x1B[32muint64\x1B[0m: \x1B[34m0000000b0000000b\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          dbgbcr:\x1B[32muint32\x1B[0m: \x1B[34m000c000c\x1B[0m\n"
      "          dbgbvr:\x1B[32muint64\x1B[0m: \x1B[34m0000000c0000000c\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          dbgbcr:\x1B[32muint32\x1B[0m: \x1B[34m000d000d\x1B[0m\n"
      "          dbgbvr:\x1B[32muint64\x1B[0m: \x1B[34m0000000d0000000d\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          dbgbcr:\x1B[32muint32\x1B[0m: \x1B[34m000e000e\x1B[0m\n"
      "          dbgbvr:\x1B[32muint64\x1B[0m: \x1B[34m0000000e0000000e\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          dbgbcr:\x1B[32muint32\x1B[0m: \x1B[34m000f000f\x1B[0m\n"
      "          dbgbvr:\x1B[32muint64\x1B[0m: \x1B[34m0000000f0000000f\x1B[0m\n"
      "        }\n"
      "      }\n"
      "      hw_bps_count:\x1B[32muint8\x1B[0m: \x1B[34m03\x1B[0m\n"
      "      hw_wps:\x1B[32mzx_thread_state_debug_regs_aarch64_wp_t\x1B[0m[]: {\n"
      "        {\n"
      "          dbgwcr:\x1B[32muint32\x1B[0m: \x1B[34m00100010\x1B[0m\n"
      "          dbgwvr:\x1B[32muint64\x1B[0m: \x1B[34m0000001000000010\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          dbgwcr:\x1B[32muint32\x1B[0m: \x1B[34m00110011\x1B[0m\n"
      "          dbgwvr:\x1B[32muint64\x1B[0m: \x1B[34m0000001100000011\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          dbgwcr:\x1B[32muint32\x1B[0m: \x1B[34m00120012\x1B[0m\n"
      "          dbgwvr:\x1B[32muint64\x1B[0m: \x1B[34m0000001200000012\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          dbgwcr:\x1B[32muint32\x1B[0m: \x1B[34m00130013\x1B[0m\n"
      "          dbgwvr:\x1B[32muint64\x1B[0m: \x1B[34m0000001300000013\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          dbgwcr:\x1B[32muint32\x1B[0m: \x1B[34m00140014\x1B[0m\n"
      "          dbgwvr:\x1B[32muint64\x1B[0m: \x1B[34m0000001400000014\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          dbgwcr:\x1B[32muint32\x1B[0m: \x1B[34m00150015\x1B[0m\n"
      "          dbgwvr:\x1B[32muint64\x1B[0m: \x1B[34m0000001500000015\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          dbgwcr:\x1B[32muint32\x1B[0m: \x1B[34m00160016\x1B[0m\n"
      "          dbgwvr:\x1B[32muint64\x1B[0m: \x1B[34m0000001600000016\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          dbgwcr:\x1B[32muint32\x1B[0m: \x1B[34m00170017\x1B[0m\n"
      "          dbgwvr:\x1B[32muint64\x1B[0m: \x1B[34m0000001700000017\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          dbgwcr:\x1B[32muint32\x1B[0m: \x1B[34m00180018\x1B[0m\n"
      "          dbgwvr:\x1B[32muint64\x1B[0m: \x1B[34m0000001800000018\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          dbgwcr:\x1B[32muint32\x1B[0m: \x1B[34m00190019\x1B[0m\n"
      "          dbgwvr:\x1B[32muint64\x1B[0m: \x1B[34m0000001900000019\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          dbgwcr:\x1B[32muint32\x1B[0m: \x1B[34m001a001a\x1B[0m\n"
      "          dbgwvr:\x1B[32muint64\x1B[0m: \x1B[34m0000001a0000001a\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          dbgwcr:\x1B[32muint32\x1B[0m: \x1B[34m001b001b\x1B[0m\n"
      "          dbgwvr:\x1B[32muint64\x1B[0m: \x1B[34m0000001b0000001b\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          dbgwcr:\x1B[32muint32\x1B[0m: \x1B[34m001c001c\x1B[0m\n"
      "          dbgwvr:\x1B[32muint64\x1B[0m: \x1B[34m0000001c0000001c\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          dbgwcr:\x1B[32muint32\x1B[0m: \x1B[34m001d001d\x1B[0m\n"
      "          dbgwvr:\x1B[32muint64\x1B[0m: \x1B[34m0000001d0000001d\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          dbgwcr:\x1B[32muint32\x1B[0m: \x1B[34m001e001e\x1B[0m\n"
      "          dbgwvr:\x1B[32muint64\x1B[0m: \x1B[34m0000001e0000001e\x1B[0m\n"
      "        }\n"
      "        {\n"
      "          dbgwcr:\x1B[32muint32\x1B[0m: \x1B[34m001f001f\x1B[0m\n"
      "          dbgwvr:\x1B[32muint64\x1B[0m: \x1B[34m0000001f0000001f\x1B[0m\n"
      "        }\n"
      "      }\n"
      "      hw_wps_count:\x1B[32muint8\x1B[0m: \x1B[34m02\x1B[0m\n"
      "      esr:\x1B[32muint32\x1B[0m: \x1B[34meeeeffff\x1B[0m\n"
      "    }\n"
      "  -> \x1B[32mZX_OK\x1B[0m\n");
}

TEST_F(InterceptionWorkflowTestX64, ZxThreadWriteStateDebugRegsX64) {
  zx_thread_state_debug_regs_x86_t buffer;
  memset(&buffer, 0, sizeof(buffer));
  constexpr uint64_t kIncrement = 0x100000001UL;
  uint64_t value = 0;
  for (size_t i = 0; i < sizeof(buffer.dr) / sizeof(buffer.dr[0]); ++i) {
    buffer.dr[i] = value;
    value += kIncrement;
  }
  buffer.dr6 = 0x66666666;
  buffer.dr7 = 0x77777777;
  THREAD_WRITE_STATE_DISPLAY_TEST_CONTENT(
      ZX_OK, ZX_THREAD_STATE_DEBUG_REGS, buffer,
      "\ntest_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
      "zx_thread_write_state("
      "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
      "kind:\x1B[32mzx_thread_state_topic_t\x1B[0m: \x1B[34mZX_THREAD_STATE_DEBUG_REGS\x1B[0m, "
      "buffer_size:\x1B[32msize_t\x1B[0m: \x1B[34m48\x1B[0m)\n"
      "    regs:\x1B[32mzx_thread_state_debug_regs_x86_t\x1B[0m: {\n"
      "      dr:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000000000000000\x1B[0m, \x1B[34m0000000100000001\x1B[0m, "
      "\x1B[34m0000000200000002\x1B[0m, \x1B[34m0000000300000003\x1B[0m\n"
      "      dr6:\x1B[32muint64\x1B[0m: \x1B[34m0000000066666666\x1B[0m\n"
      "      dr7:\x1B[32muint64\x1B[0m: \x1B[34m0000000077777777\x1B[0m\n"
      "    }\n"
      "  -> \x1B[32mZX_OK\x1B[0m\n");
}

#define THREAD_WRITE_STATE_SINGLE_STEP_DISPLAY_TEST(name, errno, value, expected)            \
  TEST_F(InterceptionWorkflowTestX64, name) {                                                \
    uint32_t single_step = value;                                                            \
    THREAD_WRITE_STATE_DISPLAY_TEST_CONTENT(errno, ZX_THREAD_STATE_SINGLE_STEP, single_step, \
                                            expected);                                       \
  }                                                                                          \
  TEST_F(InterceptionWorkflowTestArm, name) {                                                \
    uint32_t single_step = value;                                                            \
    THREAD_WRITE_STATE_DISPLAY_TEST_CONTENT(errno, ZX_THREAD_STATE_SINGLE_STEP, single_step, \
                                            expected);                                       \
  }

THREAD_WRITE_STATE_SINGLE_STEP_DISPLAY_TEST(
    ZxThreadWriteStateSingleStep0, ZX_OK, 0,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_thread_write_state("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "kind:\x1B[32mzx_thread_state_topic_t\x1B[0m: \x1B[34mZX_THREAD_STATE_SINGLE_STEP\x1B[0m, "
    "buffer_size:\x1B[32msize_t\x1B[0m: \x1B[34m4\x1B[0m, "
    "single_step:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

THREAD_WRITE_STATE_SINGLE_STEP_DISPLAY_TEST(
    ZxThreadWriteStateSingleStep1, ZX_OK, 1,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_thread_write_state("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "kind:\x1B[32mzx_thread_state_topic_t\x1B[0m: \x1B[34mZX_THREAD_STATE_SINGLE_STEP\x1B[0m, "
    "buffer_size:\x1B[32msize_t\x1B[0m: \x1B[34m4\x1B[0m, "
    "single_step:\x1B[32muint32\x1B[0m: \x1B[34m1\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

TEST_F(InterceptionWorkflowTestX64, ZxThreadWriteStateX86RegisterFs) {
  zx_thread_x86_register_fs_t reg = 0x123456789abcdef0UL;
  THREAD_WRITE_STATE_DISPLAY_TEST_CONTENT(
      ZX_OK, ZX_THREAD_X86_REGISTER_FS, reg,
      "\n"
      "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
      "zx_thread_write_state("
      "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
      "kind:\x1B[32mzx_thread_state_topic_t\x1B[0m: \x1B[34mZX_THREAD_X86_REGISTER_FS\x1B[0m, "
      "buffer_size:\x1B[32msize_t\x1B[0m: \x1B[34m8\x1B[0m, "
      "reg:\x1B[32muint64\x1B[0m: \x1B[34m123456789abcdef0\x1B[0m)\n"
      "  -> \x1B[32mZX_OK\x1B[0m\n");
}

TEST_F(InterceptionWorkflowTestX64, ZxThreadWriteStateX86RegisterGs) {
  zx_thread_x86_register_fs_t reg = 0x123456789abcdef0UL;
  THREAD_WRITE_STATE_DISPLAY_TEST_CONTENT(
      ZX_OK, ZX_THREAD_X86_REGISTER_GS, reg,
      "\n"
      "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
      "zx_thread_write_state("
      "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
      "kind:\x1B[32mzx_thread_state_topic_t\x1B[0m: \x1B[34mZX_THREAD_X86_REGISTER_GS\x1B[0m, "
      "buffer_size:\x1B[32msize_t\x1B[0m: \x1B[34m8\x1B[0m, "
      "reg:\x1B[32muint64\x1B[0m: \x1B[34m123456789abcdef0\x1B[0m)\n"
      "  -> \x1B[32mZX_OK\x1B[0m\n");
}

}  // namespace fidlcat
