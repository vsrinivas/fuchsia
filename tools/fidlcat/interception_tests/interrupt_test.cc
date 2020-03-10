// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"

namespace fidlcat {

// zx_interrupt_create tests.

std::unique_ptr<SystemCallTest> ZxInterruptCreate(int64_t result, std::string_view result_name,
                                                  zx_handle_t src_obj, uint32_t src_num,
                                                  uint32_t options, zx_handle_t* out_handle) {
  auto value = std::make_unique<SystemCallTest>("zx_interrupt_create", result, result_name);
  value->AddInput(src_obj);
  value->AddInput(src_num);
  value->AddInput(options);
  value->AddInput(reinterpret_cast<uint64_t>(out_handle));
  return value;
}

#define INTERRUPT_CREATE_DISPLAY_TEST_CONTENT(result, expected)                            \
  zx_handle_t out_handle = kHandleOut;                                                     \
  PerformDisplayTest(                                                                      \
      "$plt(zx_interrupt_create)",                                                         \
      ZxInterruptCreate(result, #result, kHandle, 1,                                       \
                        ZX_INTERRUPT_MODE_EDGE_LOW | ZX_INTERRUPT_REMAP_IRQ, &out_handle), \
      expected);

#define INTERRUPT_CREATE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                \
    INTERRUPT_CREATE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                          \
  TEST_F(InterceptionWorkflowTestArm, name) {                \
    INTERRUPT_CREATE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

INTERRUPT_CREATE_DISPLAY_TEST(
    ZxInterruptCreate, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_interrupt_create("
    "src_obj:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "src_num:\x1B[32muint32\x1B[0m: \x1B[34m1\x1B[0m, "
    "options:\x1B[32mzx_interrupt_flags_t\x1B[0m: "
    "\x1B[31mZX_INTERRUPT_MODE_EDGE_LOW | ZX_INTERRUPT_REMAP_IRQ\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (out_handle:\x1B[32mhandle\x1B[0m: \x1B[31mbde90caf\x1B[0m)\n");

// zx_interrupt_bind tests.

std::unique_ptr<SystemCallTest> ZxInterruptBind(int64_t result, std::string_view result_name,
                                                zx_handle_t handle, zx_handle_t port_handle,
                                                uint64_t key, uint32_t options) {
  auto value = std::make_unique<SystemCallTest>("zx_interrupt_bind", result, result_name);
  value->AddInput(handle);
  value->AddInput(port_handle);
  value->AddInput(key);
  value->AddInput(options);
  return value;
}

#define INTERRUPT_BIND_DISPLAY_TEST_CONTENT(result, expected) \
  PerformDisplayTest("$plt(zx_interrupt_bind)",               \
                     ZxInterruptBind(result, #result, kHandle, kHandle2, kKey, 0), expected);

#define INTERRUPT_BIND_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {              \
    INTERRUPT_BIND_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                        \
  TEST_F(InterceptionWorkflowTestArm, name) {              \
    INTERRUPT_BIND_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

INTERRUPT_BIND_DISPLAY_TEST(ZxInterruptBind, ZX_OK,
                            "\n"
                            "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                            "zx_interrupt_bind("
                            "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                            "port_handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1222\x1B[0m, "
                            "key:\x1B[32muint64\x1B[0m: \x1B[34m1234\x1B[0m, "
                            "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
                            "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_interrupt_wait tests.

std::unique_ptr<SystemCallTest> ZxInterruptWait(int64_t result, std::string_view result_name,
                                                zx_handle_t handle, zx_time_t* out_timestamp) {
  auto value = std::make_unique<SystemCallTest>("zx_interrupt_wait", result, result_name);
  value->AddInput(handle);
  value->AddInput(reinterpret_cast<uint64_t>(out_timestamp));
  return value;
}

#define INTERRUPT_WAIT_DISPLAY_TEST_CONTENT(result, expected) \
  zx_time_t out_timestamp = ZX_SEC(8000) + ZX_USEC(123);      \
  PerformDisplayTest("$plt(zx_interrupt_wait)",               \
                     ZxInterruptWait(result, #result, kHandle, &out_timestamp), expected);

#define INTERRUPT_WAIT_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {              \
    INTERRUPT_WAIT_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                        \
  TEST_F(InterceptionWorkflowTestArm, name) {              \
    INTERRUPT_WAIT_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

INTERRUPT_WAIT_DISPLAY_TEST(
    ZxInterruptWait, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_interrupt_wait(handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m ("
    "out_timestamp:\x1B[32mzx_time_t\x1B[0m: "
    "\x1B[34m2 hours, 13 minutes, 20 seconds and 123000 nano seconds\x1B[0m)\n");

// zx_interrupt_destroy tests.

std::unique_ptr<SystemCallTest> ZxInterruptDestroy(int64_t result, std::string_view result_name,
                                                   zx_handle_t handle) {
  auto value = std::make_unique<SystemCallTest>("zx_interrupt_destroy", result, result_name);
  value->AddInput(handle);
  return value;
}

#define INTERRUPT_DESTROY_DISPLAY_TEST_CONTENT(result, expected)                                 \
  PerformDisplayTest("$plt(zx_interrupt_destroy)", ZxInterruptDestroy(result, #result, kHandle), \
                     expected);

#define INTERRUPT_DESTROY_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                 \
    INTERRUPT_DESTROY_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                           \
  TEST_F(InterceptionWorkflowTestArm, name) {                 \
    INTERRUPT_DESTROY_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

INTERRUPT_DESTROY_DISPLAY_TEST(
    ZxInterruptDestroy, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_interrupt_destroy(handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_interrupt_ack tests.

std::unique_ptr<SystemCallTest> ZxInterruptAck(int64_t result, std::string_view result_name,
                                               zx_handle_t handle) {
  auto value = std::make_unique<SystemCallTest>("zx_interrupt_ack", result, result_name);
  value->AddInput(handle);
  return value;
}

#define INTERRUPT_ACK_DISPLAY_TEST_CONTENT(result, expected) \
  PerformDisplayTest("$plt(zx_interrupt_ack)", ZxInterruptAck(result, #result, kHandle), expected);

#define INTERRUPT_ACK_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {             \
    INTERRUPT_ACK_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                       \
  TEST_F(InterceptionWorkflowTestArm, name) { INTERRUPT_ACK_DISPLAY_TEST_CONTENT(errno, expected); }

INTERRUPT_ACK_DISPLAY_TEST(
    ZxInterruptAck, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_interrupt_ack(handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_interrupt_trigger tests.

std::unique_ptr<SystemCallTest> ZxInterruptTrigger(int64_t result, std::string_view result_name,
                                                   zx_handle_t handle, uint32_t options,
                                                   zx_time_t timestamp) {
  auto value = std::make_unique<SystemCallTest>("zx_interrupt_trigger", result, result_name);
  value->AddInput(handle);
  value->AddInput(options);
  value->AddInput(timestamp);
  return value;
}

#define INTERRUPT_TRIGGER_DISPLAY_TEST_CONTENT(result, expected) \
  PerformDisplayTest("$plt(zx_interrupt_trigger)",               \
                     ZxInterruptTrigger(result, #result, kHandle, 0, ZX_SEC(8000)), expected);

#define INTERRUPT_TRIGGER_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                 \
    INTERRUPT_TRIGGER_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                           \
  TEST_F(InterceptionWorkflowTestArm, name) {                 \
    INTERRUPT_TRIGGER_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

INTERRUPT_TRIGGER_DISPLAY_TEST(
    ZxInterruptTrigger, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_interrupt_trigger("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m, "
    "timestamp:\x1B[32mzx_time_t\x1B[0m: \x1B[34m2 hours, 13 minutes, 20 seconds\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_interrupt_bind_vcpu tests.

std::unique_ptr<SystemCallTest> ZxInterruptBindVcpu(int64_t result, std::string_view result_name,
                                                    zx_handle_t handle, zx_handle_t vcpu,
                                                    uint32_t options) {
  auto value = std::make_unique<SystemCallTest>("zx_interrupt_bind_vcpu", result, result_name);
  value->AddInput(handle);
  value->AddInput(vcpu);
  value->AddInput(options);
  return value;
}

#define INTERRUPT_BIND_VCPU_DISPLAY_TEST_CONTENT(result, expected) \
  PerformDisplayTest("$plt(zx_interrupt_bind_vcpu)",               \
                     ZxInterruptBindVcpu(result, #result, kHandle, kHandle2, 0), expected);

#define INTERRUPT_BIND_VCPU_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                   \
    INTERRUPT_BIND_VCPU_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                             \
  TEST_F(InterceptionWorkflowTestArm, name) {                   \
    INTERRUPT_BIND_VCPU_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

INTERRUPT_BIND_VCPU_DISPLAY_TEST(ZxInterruptBindVcpu, ZX_OK,
                                 "\n"
                                 "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                                 "zx_interrupt_bind_vcpu("
                                 "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                                 "vcpu:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1222\x1B[0m, "
                                 "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
                                 "  -> \x1B[32mZX_OK\x1B[0m\n");

}  // namespace fidlcat
