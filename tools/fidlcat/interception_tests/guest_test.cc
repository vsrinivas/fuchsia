// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"

namespace fidlcat {

// zx_guest_create tests.

std::unique_ptr<SystemCallTest> ZxGuestCreate(int64_t result, std::string_view result_name,
                                              zx_handle_t resource, uint32_t options,
                                              zx_handle_t* guest_handle, zx_handle_t* vmar_handle) {
  auto value = std::make_unique<SystemCallTest>("zx_guest_create", result, result_name);
  value->AddInput(resource);
  value->AddInput(options);
  value->AddInput(reinterpret_cast<uint64_t>(guest_handle));
  value->AddInput(reinterpret_cast<uint64_t>(vmar_handle));
  return value;
}

#define GUEST_CREATE_DISPLAY_TEST_CONTENT(result, expected)                                   \
  zx_handle_t guest_handle = kHandleOut;                                                      \
  zx_handle_t vmar_handle = kHandleOut2;                                                      \
  PerformDisplayTest("$plt(zx_guest_create)",                                                 \
                     ZxGuestCreate(result, #result, kHandle, 0, &guest_handle, &vmar_handle), \
                     expected);

#define GUEST_CREATE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {            \
    GUEST_CREATE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                      \
  TEST_F(InterceptionWorkflowTestArm, name) { GUEST_CREATE_DISPLAY_TEST_CONTENT(errno, expected); }

GUEST_CREATE_DISPLAY_TEST(ZxGuestCreate, ZX_OK,
                          "\n"
                          "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                          "zx_guest_create("
                          "resource:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                          "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
                          "  -> \x1B[32mZX_OK\x1B[0m ("
                          "guest_handle:\x1B[32mhandle\x1B[0m: \x1B[31mbde90caf\x1B[0m, "
                          "vmar_handle:\x1B[32mhandle\x1B[0m: \x1B[31mbde90222\x1B[0m)\n");

// zx_guest_set_trap tests.

std::unique_ptr<SystemCallTest> ZxGuestSetTrap(int64_t result, std::string_view result_name,
                                               zx_handle_t handle, uint32_t kind, zx_vaddr_t addr,
                                               size_t size, zx_handle_t port_handle, uint64_t key) {
  auto value = std::make_unique<SystemCallTest>("zx_guest_set_trap", result, result_name);
  value->AddInput(handle);
  value->AddInput(kind);
  value->AddInput(addr);
  value->AddInput(size);
  value->AddInput(port_handle);
  value->AddInput(key);
  return value;
}

#define GUEST_SET_TRAP_DISPLAY_TEST_CONTENT(result, expected)                                 \
  PerformDisplayTest(                                                                         \
      "$plt(zx_guest_set_trap)",                                                              \
      ZxGuestSetTrap(result, #result, kHandle, ZX_GUEST_TRAP_IO, 0x1234, 16, kHandle2, kKey), \
      expected);

#define GUEST_SET_TRAP_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {              \
    GUEST_SET_TRAP_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                        \
  TEST_F(InterceptionWorkflowTestArm, name) {              \
    GUEST_SET_TRAP_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

GUEST_SET_TRAP_DISPLAY_TEST(ZxGuestSetTrap, ZX_OK,
                            "\n"
                            "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                            "zx_guest_set_trap("
                            "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                            "kind:\x1B[32mzx_guest_trap_t\x1B[0m: \x1B[31mZX_GUEST_TRAP_IO\x1B[0m, "
                            "addr:\x1B[32mzx_vaddr_t\x1B[0m: \x1B[34m0000000000001234\x1B[0m, "
                            "size:\x1B[32msize_t\x1B[0m: \x1B[34m16\x1B[0m, "
                            "port_handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1222\x1B[0m, "
                            "key:\x1B[32muint64\x1B[0m: \x1B[34m1234\x1B[0m)\n"
                            "  -> \x1B[32mZX_OK\x1B[0m\n");

}  // namespace fidlcat
