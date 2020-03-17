// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"

#include <zircon/syscalls/port.h>

namespace fidlcat {

// zx_vcpu_create tests.

std::unique_ptr<SystemCallTest> ZxVcpuCreate(int64_t result, std::string_view result_name,
                                             zx_handle_t guest, uint32_t options, zx_vaddr_t entry,
                                             zx_handle_t* out) {
  auto value = std::make_unique<SystemCallTest>("zx_vcpu_create", result, result_name);
  value->AddInput(guest);
  value->AddInput(options);
  value->AddInput(entry);
  value->AddInput(reinterpret_cast<uint64_t>(out));
  return value;
}

#define VCPU_CREATE_DISPLAY_TEST_CONTENT(result, expected) \
  zx_handle_t out = kHandleOut;                            \
  PerformDisplayTest("$plt(zx_vcpu_create)",               \
                     ZxVcpuCreate(result, #result, kHandle, 0, 0x123456, &out), expected);

#define VCPU_CREATE_DISPLAY_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { VCPU_CREATE_DISPLAY_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { VCPU_CREATE_DISPLAY_TEST_CONTENT(errno, expected); }

VCPU_CREATE_DISPLAY_TEST(
    ZxVcpuCreate, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_vcpu_create("
    "guest:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m, "
    "entry:\x1B[32mzx_vaddr_t\x1B[0m: \x1B[34m0000000000123456\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (out:\x1B[32mhandle\x1B[0m: \x1B[31mbde90caf\x1B[0m)\n");

// zx_vcpu_resume tests.

std::unique_ptr<SystemCallTest> ZxVcpuResume(int64_t result, std::string_view result_name,
                                             zx_handle_t handle, zx_port_packet_t* packet) {
  auto value = std::make_unique<SystemCallTest>("zx_vcpu_resume", result, result_name);
  value->AddInput(handle);
  value->AddInput(reinterpret_cast<uint64_t>(packet));
  return value;
}

#define VCPU_RESUME_DISPLAY_TEST_CONTENT(result, expected)                                    \
  zx_port_packet_t packet = {.key = kKey,                                                     \
                             .type = ZX_PKT_TYPE_GUEST_VCPU,                                  \
                             .status = ZX_OK,                                                 \
                             .guest_vcpu = {.startup.id = 1234,                               \
                                            .startup.entry = 0x123456,                        \
                                            .type = ZX_PKT_GUEST_VCPU_STARTUP,                \
                                            .reserved = 0}};                                  \
  PerformDisplayTest("$plt(zx_vcpu_resume)", ZxVcpuResume(result, #result, kHandle, &packet), \
                     expected);

#define VCPU_RESUME_DISPLAY_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { VCPU_RESUME_DISPLAY_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { VCPU_RESUME_DISPLAY_TEST_CONTENT(errno, expected); }

VCPU_RESUME_DISPLAY_TEST(
    ZxVcpuResume, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_vcpu_resume(handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n"
    "      packet:\x1B[32mzx_port_packet_t\x1B[0m: {\n"
    "        key:\x1B[32muint64\x1B[0m: \x1B[34m1234\x1B[0m\n"
    "        type:\x1B[32mzx_port_packet_t::type\x1B[0m: \x1B[34mZX_PKT_TYPE_GUEST_VCPU\x1B[0m\n"
    "        status:\x1B[32mstatus_t\x1B[0m: \x1B[32mZX_OK\x1B[0m\n"
    "        guest_vcpu:\x1B[32mzx_packet_guest_vcpu_t\x1B[0m: {\n"
    "          type:\x1B[32mzx_packet_guest_vcpu_t::type\x1B[0m: "
    "\x1B[34mZX_PKT_GUEST_VCPU_STARTUP\x1B[0m\n"
    "          startup:\x1B[32mzx_packet_guest_vcpu_startup_t\x1B[0m: {\n"
    "            id:\x1B[32muint64\x1B[0m: \x1B[34m1234\x1B[0m\n"
    "            entry:\x1B[32mzx_gpaddr_t\x1B[0m: \x1B[34m0000000000123456\x1B[0m\n"
    "          }\n"
    "          reserved:\x1B[32muint64\x1B[0m: \x1B[34m0\x1B[0m\n"
    "        }\n"
    "      }\n");

// zx_vcpu_interrupt tests.

std::unique_ptr<SystemCallTest> ZxVcpuInterrupt(int64_t result, std::string_view result_name,
                                                zx_handle_t handle, uint32_t vector) {
  auto value = std::make_unique<SystemCallTest>("zx_vcpu_interrupt", result, result_name);
  value->AddInput(handle);
  value->AddInput(vector);
  return value;
}

#define VCPU_INTERRUPT_DISPLAY_TEST_CONTENT(result, expected)                                  \
  PerformDisplayTest("$plt(zx_vcpu_interrupt)", ZxVcpuInterrupt(result, #result, kHandle, 10), \
                     expected);

#define VCPU_INTERRUPT_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {              \
    VCPU_INTERRUPT_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                        \
  TEST_F(InterceptionWorkflowTestArm, name) {              \
    VCPU_INTERRUPT_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

VCPU_INTERRUPT_DISPLAY_TEST(ZxVcpuInterrupt, ZX_OK,
                            "\n"
                            "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                            "zx_vcpu_interrupt("
                            "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                            "vector:\x1B[32muint32\x1B[0m: \x1B[34m10\x1B[0m)\n"
                            "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_vcpu_read_state tests.

std::unique_ptr<SystemCallTest> ZxVcpuReadState(int64_t result, std::string_view result_name,
                                                zx_handle_t handle, uint32_t kind, void* buffer,
                                                size_t buffer_size) {
  auto value = std::make_unique<SystemCallTest>("zx_vcpu_read_state", result, result_name);
  value->AddInput(handle);
  value->AddInput(kind);
  value->AddInput(reinterpret_cast<uint64_t>(buffer));
  value->AddInput(buffer_size);
  return value;
}

#define VCPU_READ_STATE_DISPLAY_TEST_CONTENT(result, buffer, expected)                   \
  PerformDisplayTest(                                                                    \
      "$plt(zx_vcpu_read_state)",                                                        \
      ZxVcpuReadState(result, #result, kHandle, ZX_VCPU_STATE, &buffer, sizeof(buffer)), \
      expected);

TEST_F(InterceptionWorkflowTestArm, ZxVcpuReadStateAArch64) {
  zx_vcpu_state_aarch64_t buffer;
  for (int i = 0; i < 31; ++i) {
    buffer.x[i] = i;
  }
  buffer.sp = 0x1234576;
  buffer.cpsr = 0xe0000000;
  VCPU_READ_STATE_DISPLAY_TEST_CONTENT(
      ZX_OK, buffer,
      "\n"
      "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
      "zx_vcpu_read_state("
      "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
      "kind:\x1B[32mzx_vcpu_t\x1B[0m: \x1B[31mZX_VCPU_STATE\x1B[0m)\n"
      "  -> \x1B[32mZX_OK\x1B[0m\n"
      "      buffer:\x1B[32mzx_vcpu_state_aarch64_t\x1B[0m: {\n"
      "        x:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000000000000000\x1B[0m, \x1B[34m0000000000000001\x1B[0m, "
      "\x1B[34m0000000000000002\x1B[0m, \x1B[34m0000000000000003\x1B[0m, "
      "\x1B[34m0000000000000004\x1B[0m, \x1B[34m0000000000000005\x1B[0m, "
      "\x1B[34m0000000000000006\x1B[0m, \x1B[34m0000000000000007\x1B[0m, "
      "\x1B[34m0000000000000008\x1B[0m, \x1B[34m0000000000000009\x1B[0m, "
      "\x1B[34m000000000000000a\x1B[0m, \x1B[34m000000000000000b\x1B[0m, "
      "\x1B[34m000000000000000c\x1B[0m, \x1B[34m000000000000000d\x1B[0m, "
      "\x1B[34m000000000000000e\x1B[0m, \x1B[34m000000000000000f\x1B[0m, "
      "\x1B[34m0000000000000010\x1B[0m, \x1B[34m0000000000000011\x1B[0m, "
      "\x1B[34m0000000000000012\x1B[0m, \x1B[34m0000000000000013\x1B[0m, "
      "\x1B[34m0000000000000014\x1B[0m, \x1B[34m0000000000000015\x1B[0m, "
      "\x1B[34m0000000000000016\x1B[0m, \x1B[34m0000000000000017\x1B[0m, "
      "\x1B[34m0000000000000018\x1B[0m, \x1B[34m0000000000000019\x1B[0m, "
      "\x1B[34m000000000000001a\x1B[0m, \x1B[34m000000000000001b\x1B[0m, "
      "\x1B[34m000000000000001c\x1B[0m, \x1B[34m000000000000001d\x1B[0m, "
      "\x1B[34m000000000000001e\x1B[0m\n"
      "        sp:\x1B[32muint64\x1B[0m: \x1B[34m0000000001234576\x1B[0m\n"
      "        cpsr:\x1B[32muint32\x1B[0m: \x1B[34me0000000\x1B[0m\n"
      "      }\n");
}

TEST_F(InterceptionWorkflowTestX64, ZxVcpuReadStateX86) {
  zx_vcpu_state_x86_t buffer = {.rax = 1,
                                .rcx = 2,
                                .rdx = 3,
                                .rbx = 4,
                                .rsp = 5,
                                .rbp = 6,
                                .rsi = 7,
                                .rdi = 8,
                                .r8 = 9,
                                .r9 = 10,
                                .r10 = 11,
                                .r11 = 12,
                                .r12 = 13,
                                .r13 = 14,
                                .r14 = 15,
                                .r15 = 16,
                                .rflags = 0x1234};
  VCPU_READ_STATE_DISPLAY_TEST_CONTENT(
      ZX_OK, buffer,
      "\n"
      "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
      "zx_vcpu_read_state("
      "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
      "kind:\x1B[32mzx_vcpu_t\x1B[0m: \x1B[31mZX_VCPU_STATE\x1B[0m)\n"
      "  -> \x1B[32mZX_OK\x1B[0m\n"
      "      buffer:\x1B[32mzx_vcpu_state_x86_t\x1B[0m: {\n"
      "        rax:\x1B[32muint64\x1B[0m: \x1B[34m0000000000000001\x1B[0m\n"
      "        rcx:\x1B[32muint64\x1B[0m: \x1B[34m0000000000000002\x1B[0m\n"
      "        rdx:\x1B[32muint64\x1B[0m: \x1B[34m0000000000000003\x1B[0m\n"
      "        rbx:\x1B[32muint64\x1B[0m: \x1B[34m0000000000000004\x1B[0m\n"
      "        rsp:\x1B[32muint64\x1B[0m: \x1B[34m0000000000000005\x1B[0m\n"
      "        rbp:\x1B[32muint64\x1B[0m: \x1B[34m0000000000000006\x1B[0m\n"
      "        rsi:\x1B[32muint64\x1B[0m: \x1B[34m0000000000000007\x1B[0m\n"
      "        rdi:\x1B[32muint64\x1B[0m: \x1B[34m0000000000000008\x1B[0m\n"
      "        r8:\x1B[32muint64\x1B[0m: \x1B[34m0000000000000009\x1B[0m\n"
      "        r9:\x1B[32muint64\x1B[0m: \x1B[34m000000000000000a\x1B[0m\n"
      "        r10:\x1B[32muint64\x1B[0m: \x1B[34m000000000000000b\x1B[0m\n"
      "        r11:\x1B[32muint64\x1B[0m: \x1B[34m000000000000000c\x1B[0m\n"
      "        r12:\x1B[32muint64\x1B[0m: \x1B[34m000000000000000d\x1B[0m\n"
      "        r13:\x1B[32muint64\x1B[0m: \x1B[34m000000000000000e\x1B[0m\n"
      "        r14:\x1B[32muint64\x1B[0m: \x1B[34m000000000000000f\x1B[0m\n"
      "        r15:\x1B[32muint64\x1B[0m: \x1B[34m0000000000000010\x1B[0m\n"
      "        rflags:\x1B[32muint64\x1B[0m: \x1B[34m0000000000001234\x1B[0m\n"
      "      }\n");
}

// zx_vcpu_write_state tests.

std::unique_ptr<SystemCallTest> ZxVcpuWriteState(int64_t result, std::string_view result_name,
                                                 zx_handle_t handle, uint32_t kind,
                                                 const void* buffer, size_t buffer_size) {
  auto value = std::make_unique<SystemCallTest>("zx_vcpu_write_state", result, result_name);
  value->AddInput(handle);
  value->AddInput(kind);
  value->AddInput(reinterpret_cast<uint64_t>(buffer));
  value->AddInput(buffer_size);
  return value;
}

#define VCPU_WRITE_STATE_DISPLAY_TEST_CONTENT(result, buffer, expected)                  \
  PerformDisplayTest(                                                                    \
      "$plt(zx_vcpu_write_state)",                                                       \
      ZxVcpuReadState(result, #result, kHandle, ZX_VCPU_STATE, &buffer, sizeof(buffer)), \
      expected);

TEST_F(InterceptionWorkflowTestArm, ZxVcpuWriteStateAArch64) {
  zx_vcpu_state_aarch64_t buffer;
  for (int i = 0; i < 31; ++i) {
    buffer.x[i] = i;
  }
  buffer.sp = 0x1234576;
  buffer.cpsr = 0xe0000000;
  VCPU_WRITE_STATE_DISPLAY_TEST_CONTENT(
      ZX_OK, buffer,
      "\n"
      "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
      "zx_vcpu_write_state("
      "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
      "kind:\x1B[32mzx_vcpu_t\x1B[0m: \x1B[31mZX_VCPU_STATE\x1B[0m)\n"
      "    buffer:\x1B[32mzx_vcpu_state_aarch64_t\x1B[0m: {\n"
      "      x:\x1B[32muint64[]\x1B[0m: "
      "\x1B[34m0000000000000000\x1B[0m, \x1B[34m0000000000000001\x1B[0m, "
      "\x1B[34m0000000000000002\x1B[0m, \x1B[34m0000000000000003\x1B[0m, "
      "\x1B[34m0000000000000004\x1B[0m, \x1B[34m0000000000000005\x1B[0m, "
      "\x1B[34m0000000000000006\x1B[0m, \x1B[34m0000000000000007\x1B[0m, "
      "\x1B[34m0000000000000008\x1B[0m, \x1B[34m0000000000000009\x1B[0m, "
      "\x1B[34m000000000000000a\x1B[0m, \x1B[34m000000000000000b\x1B[0m, "
      "\x1B[34m000000000000000c\x1B[0m, \x1B[34m000000000000000d\x1B[0m, "
      "\x1B[34m000000000000000e\x1B[0m, \x1B[34m000000000000000f\x1B[0m, "
      "\x1B[34m0000000000000010\x1B[0m, \x1B[34m0000000000000011\x1B[0m, "
      "\x1B[34m0000000000000012\x1B[0m, \x1B[34m0000000000000013\x1B[0m, "
      "\x1B[34m0000000000000014\x1B[0m, \x1B[34m0000000000000015\x1B[0m, "
      "\x1B[34m0000000000000016\x1B[0m, \x1B[34m0000000000000017\x1B[0m, "
      "\x1B[34m0000000000000018\x1B[0m, \x1B[34m0000000000000019\x1B[0m, "
      "\x1B[34m000000000000001a\x1B[0m, \x1B[34m000000000000001b\x1B[0m, "
      "\x1B[34m000000000000001c\x1B[0m, \x1B[34m000000000000001d\x1B[0m, "
      "\x1B[34m000000000000001e\x1B[0m\n"
      "      sp:\x1B[32muint64\x1B[0m: \x1B[34m0000000001234576\x1B[0m\n"
      "      cpsr:\x1B[32muint32\x1B[0m: \x1B[34me0000000\x1B[0m\n"
      "    }\n"
      "  -> \x1B[32mZX_OK\x1B[0m\n");
}

TEST_F(InterceptionWorkflowTestX64, ZxVcpuWriteStateX86) {
  zx_vcpu_state_x86_t buffer = {.rax = 1,
                                .rcx = 2,
                                .rdx = 3,
                                .rbx = 4,
                                .rsp = 5,
                                .rbp = 6,
                                .rsi = 7,
                                .rdi = 8,
                                .r8 = 9,
                                .r9 = 10,
                                .r10 = 11,
                                .r11 = 12,
                                .r12 = 13,
                                .r13 = 14,
                                .r14 = 15,
                                .r15 = 16,
                                .rflags = 0x1234};
  VCPU_WRITE_STATE_DISPLAY_TEST_CONTENT(
      ZX_OK, buffer,
      "\n"
      "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
      "zx_vcpu_write_state("
      "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
      "kind:\x1B[32mzx_vcpu_t\x1B[0m: \x1B[31mZX_VCPU_STATE\x1B[0m)\n"
      "    buffer:\x1B[32mzx_vcpu_state_x86_t\x1B[0m: {\n"
      "      rax:\x1B[32muint64\x1B[0m: \x1B[34m0000000000000001\x1B[0m\n"
      "      rcx:\x1B[32muint64\x1B[0m: \x1B[34m0000000000000002\x1B[0m\n"
      "      rdx:\x1B[32muint64\x1B[0m: \x1B[34m0000000000000003\x1B[0m\n"
      "      rbx:\x1B[32muint64\x1B[0m: \x1B[34m0000000000000004\x1B[0m\n"
      "      rsp:\x1B[32muint64\x1B[0m: \x1B[34m0000000000000005\x1B[0m\n"
      "      rbp:\x1B[32muint64\x1B[0m: \x1B[34m0000000000000006\x1B[0m\n"
      "      rsi:\x1B[32muint64\x1B[0m: \x1B[34m0000000000000007\x1B[0m\n"
      "      rdi:\x1B[32muint64\x1B[0m: \x1B[34m0000000000000008\x1B[0m\n"
      "      r8:\x1B[32muint64\x1B[0m: \x1B[34m0000000000000009\x1B[0m\n"
      "      r9:\x1B[32muint64\x1B[0m: \x1B[34m000000000000000a\x1B[0m\n"
      "      r10:\x1B[32muint64\x1B[0m: \x1B[34m000000000000000b\x1B[0m\n"
      "      r11:\x1B[32muint64\x1B[0m: \x1B[34m000000000000000c\x1B[0m\n"
      "      r12:\x1B[32muint64\x1B[0m: \x1B[34m000000000000000d\x1B[0m\n"
      "      r13:\x1B[32muint64\x1B[0m: \x1B[34m000000000000000e\x1B[0m\n"
      "      r14:\x1B[32muint64\x1B[0m: \x1B[34m000000000000000f\x1B[0m\n"
      "      r15:\x1B[32muint64\x1B[0m: \x1B[34m0000000000000010\x1B[0m\n"
      "      rflags:\x1B[32muint64\x1B[0m: \x1B[34m0000000000001234\x1B[0m\n"
      "    }\n"
      "  -> \x1B[32mZX_OK\x1B[0m\n");
}

#define VCPU_WRITE_STATE_IO_DISPLAY_TEST_CONTENT(result, expected) \
  zx_vcpu_io_t buffer = {.access_size = 4, .u32 = 0x12345678};     \
  PerformDisplayTest(                                              \
      "$plt(zx_vcpu_write_state)",                                 \
      ZxVcpuReadState(result, #result, kHandle, ZX_VCPU_IO, &buffer, sizeof(buffer)), expected);

#define VCPU_WRITE_STATE_IO_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                   \
    VCPU_WRITE_STATE_IO_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                             \
  TEST_F(InterceptionWorkflowTestArm, name) {                   \
    VCPU_WRITE_STATE_IO_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

VCPU_WRITE_STATE_IO_DISPLAY_TEST(
    ZxVcpuWriteStateIo, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_vcpu_write_state("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "kind:\x1B[32mzx_vcpu_t\x1B[0m: \x1B[31mZX_VCPU_IO\x1B[0m)\n"
    "    buffer:\x1B[32mzx_vcpu_io_t\x1B[0m: {\n"
    "      access_size:\x1B[32muint8\x1B[0m: \x1B[34m4\x1B[0m\n"
    "      u8:\x1B[32muint8\x1B[0m: \x1B[34m78\x1B[0m\n"
    "      u16:\x1B[32muint16\x1B[0m: \x1B[34m5678\x1B[0m\n"
    "      u32:\x1B[32muint32\x1B[0m: \x1B[34m12345678\x1B[0m\n"
    "      data:\x1B[32muint8[]\x1B[0m: "
    "\x1B[34m78\x1B[0m, \x1B[34m56\x1B[0m, \x1B[34m34\x1B[0m, \x1B[34m12\x1B[0m\n"
    "    }\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

}  // namespace fidlcat
