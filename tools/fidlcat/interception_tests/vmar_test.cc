// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"

namespace fidlcat {

// zx_vmar_allocate tests.

std::unique_ptr<SystemCallTest> ZxVmarAllocate(int64_t result, std::string_view result_name,
                                               zx_handle_t parent_vmar, zx_vm_option_t options,
                                               uint64_t offset, uint64_t size,
                                               zx_handle_t* child_vmar, zx_vaddr_t* child_addr) {
  auto value = std::make_unique<SystemCallTest>("zx_vmar_allocate", result, result_name);
  value->AddInput(parent_vmar);
  value->AddInput(options);
  value->AddInput(offset);
  value->AddInput(size);
  value->AddInput(reinterpret_cast<uint64_t>(child_vmar));
  value->AddInput(reinterpret_cast<uint64_t>(child_addr));
  return value;
}

#define VMAR_ALLOCATE_DISPLAY_TEST_CONTENT(result, expected)                                      \
  zx_handle_t child_vmar = kHandleOut;                                                            \
  zx_vaddr_t child_addr = 0x12345;                                                                \
  PerformDisplayTest("$plt(zx_vmar_allocate)",                                                    \
                     ZxVmarAllocate(result, #result, kHandle, ZX_VM_COMPACT | ZX_VM_CAN_MAP_READ, \
                                    0, 1024, &child_vmar, &child_addr),                           \
                     expected);

#define VMAR_ALLOCATE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {             \
    VMAR_ALLOCATE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                       \
  TEST_F(InterceptionWorkflowTestArm, name) { VMAR_ALLOCATE_DISPLAY_TEST_CONTENT(errno, expected); }

VMAR_ALLOCATE_DISPLAY_TEST(
    ZxVmarAllocate, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_vmar_allocate("
    "parent_vmar:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "options:\x1B[32mzx_vm_option_t\x1B[0m: \x1B[31mZX_VM_COMPACT | ZX_VM_CAN_MAP_READ\x1B[0m, "
    "offset:\x1B[32muint64\x1B[0m: \x1B[34m0\x1B[0m, "
    "size:\x1B[32muint64\x1B[0m: \x1B[34m1024\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m ("
    "child_vmar:\x1B[32mhandle\x1B[0m: \x1B[31mbde90caf\x1B[0m, "
    "child_addr:\x1B[32mzx_vaddr_t\x1B[0m: \x1B[34m0000000000012345\x1B[0m)\n");

// zx_vmar_destroy tests.

std::unique_ptr<SystemCallTest> ZxVmarDestroy(int64_t result, std::string_view result_name,
                                              zx_handle_t handle) {
  auto value = std::make_unique<SystemCallTest>("zx_vmar_destroy", result, result_name);
  value->AddInput(handle);
  return value;
}

#define VMAR_DESTROY_DISPLAY_TEST_CONTENT(result, expected) \
  PerformDisplayTest("$plt(zx_vmar_destroy)", ZxVmarDestroy(result, #result, kHandle), expected);

#define VMAR_DESTROY_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {            \
    VMAR_DESTROY_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                      \
  TEST_F(InterceptionWorkflowTestArm, name) { VMAR_DESTROY_DISPLAY_TEST_CONTENT(errno, expected); }

VMAR_DESTROY_DISPLAY_TEST(ZxVmarDestroy, ZX_OK,
                          "\n"
                          "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                          "zx_vmar_destroy(handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m)\n"
                          "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_vmar_map tests.

std::unique_ptr<SystemCallTest> ZxVmarMap(int64_t result, std::string_view result_name,
                                          zx_handle_t handle, zx_vm_option_t options,
                                          uint64_t vmar_offset, zx_handle_t vmo,
                                          uint64_t vmo_offset, uint64_t len,
                                          zx_vaddr_t* mapped_addr) {
  auto value = std::make_unique<SystemCallTest>("zx_vmar_map", result, result_name);
  value->AddInput(handle);
  value->AddInput(options);
  value->AddInput(vmar_offset);
  value->AddInput(vmo);
  value->AddInput(vmo_offset);
  value->AddInput(len);
  value->AddInput(reinterpret_cast<uint64_t>(mapped_addr));
  return value;
}

#define VMAR_MAP_DISPLAY_TEST_CONTENT(result, expected)                                        \
  zx_vaddr_t mapped_addr = 0x12345;                                                            \
  PerformDisplayTest("$plt(zx_vmar_map)",                                                      \
                     ZxVmarMap(result, #result, kHandle, ZX_VM_SPECIFIC | ZX_VM_PERM_READ, 10, \
                               kHandle2, 0, 1024, &mapped_addr),                               \
                     expected);

#define VMAR_MAP_DISPLAY_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { VMAR_MAP_DISPLAY_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { VMAR_MAP_DISPLAY_TEST_CONTENT(errno, expected); }

VMAR_MAP_DISPLAY_TEST(
    ZxVmarMap, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_vmar_map("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "options:\x1B[32mzx_vm_option_t\x1B[0m: \x1B[31mZX_VM_PERM_READ | ZX_VM_SPECIFIC\x1B[0m, "
    "vmar_offset:\x1B[32muint64\x1B[0m: \x1B[34m10\x1B[0m, "
    "vmo:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1222\x1B[0m, "
    "vmo_offset:\x1B[32muint64\x1B[0m: \x1B[34m0\x1B[0m, "
    "len:\x1B[32muint64\x1B[0m: \x1B[34m1024\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m ("
    "mapped_addr:\x1B[32mzx_vaddr_t\x1B[0m: \x1B[34m0000000000012345\x1B[0m)\n");

// zx_vmar_unmap tests.

std::unique_ptr<SystemCallTest> ZxVmarUnmap(int64_t result, std::string_view result_name,
                                            zx_handle_t handle, zx_vaddr_t addr, uint64_t len) {
  auto value = std::make_unique<SystemCallTest>("zx_vmar_unmap", result, result_name);
  value->AddInput(handle);
  value->AddInput(addr);
  value->AddInput(len);
  return value;
}

#define VMAR_UNMAP_DISPLAY_TEST_CONTENT(result, expected)                                         \
  PerformDisplayTest("$plt(zx_vmar_unmap)", ZxVmarUnmap(result, #result, kHandle, 0x12345, 1024), \
                     expected);

#define VMAR_UNMAP_DISPLAY_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { VMAR_UNMAP_DISPLAY_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { VMAR_UNMAP_DISPLAY_TEST_CONTENT(errno, expected); }

VMAR_UNMAP_DISPLAY_TEST(ZxVmarUnmap, ZX_OK,
                        "\n"
                        "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                        "zx_vmar_unmap("
                        "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                        "addr:\x1B[32mzx_vaddr_t\x1B[0m: \x1B[34m0000000000012345\x1B[0m, "
                        "len:\x1B[32muint64\x1B[0m: \x1B[34m1024\x1B[0m)\n"
                        "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_vmar_protect tests.

std::unique_ptr<SystemCallTest> ZxVmarProtect(int64_t result, std::string_view result_name,
                                              zx_handle_t handle, zx_vm_option_t options,
                                              zx_vaddr_t addr, uint64_t len) {
  auto value = std::make_unique<SystemCallTest>("zx_vmar_protect", result, result_name);
  value->AddInput(handle);
  value->AddInput(options);
  value->AddInput(addr);
  value->AddInput(len);
  return value;
}

#define VMAR_PROTECT_DISPLAY_TEST_CONTENT(result, expected)                                        \
  PerformDisplayTest("$plt(zx_vmar_protect)",                                                      \
                     ZxVmarProtect(result, #result, kHandle, ZX_VM_PERM_READ | ZX_VM_PERM_EXECUTE, \
                                   0x12345, 1024),                                                 \
                     expected);

#define VMAR_PROTECT_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {            \
    VMAR_PROTECT_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                      \
  TEST_F(InterceptionWorkflowTestArm, name) { VMAR_PROTECT_DISPLAY_TEST_CONTENT(errno, expected); }

VMAR_PROTECT_DISPLAY_TEST(
    ZxVmarProtect, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_vmar_protect("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "options:\x1B[32mzx_vm_option_t\x1B[0m: \x1B[31mZX_VM_PERM_READ | ZX_VM_PERM_EXECUTE\x1B[0m, "
    "addr:\x1B[32mzx_vaddr_t\x1B[0m: \x1B[34m0000000000012345\x1B[0m, len:\x1B[32muint64\x1B[0m: "
    "\x1B[34m1024\x1B[0m)\n  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_vmar_unmap_handle_close_thread_exit tests.

std::unique_ptr<SystemCallTest> ZxVmarUnmapHandleCloseThreadExit(int64_t result,
                                                                 std::string_view result_name,
                                                                 zx_handle_t vmar_handle,
                                                                 zx_vaddr_t addr, size_t size,
                                                                 zx_handle_t close_handle) {
  auto value = std::make_unique<SystemCallTest>("zx_vmar_unmap_handle_close_thread_exit", result,
                                                result_name);
  value->AddInput(vmar_handle);
  value->AddInput(addr);
  value->AddInput(size);
  value->AddInput(close_handle);
  return value;
}

#define VMAR_UNMAP_HANDLE_CLOSE_THREAD_EXIT_DISPLAY_TEST_CONTENT(result, expected)         \
  PerformDisplayTest(                                                                      \
      "$plt(zx_vmar_unmap_handle_close_thread_exit)",                                      \
      ZxVmarUnmapHandleCloseThreadExit(result, #result, kHandle, 0x12345, 1024, kHandle2), \
      expected);

#define VMAR_UNMAP_HANDLE_CLOSE_THREAD_EXIT_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                                   \
    VMAR_UNMAP_HANDLE_CLOSE_THREAD_EXIT_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                             \
  TEST_F(InterceptionWorkflowTestArm, name) {                                   \
    VMAR_UNMAP_HANDLE_CLOSE_THREAD_EXIT_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

VMAR_UNMAP_HANDLE_CLOSE_THREAD_EXIT_DISPLAY_TEST(
    ZxVmarUnmapHandleCloseThreadExit, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_vmar_unmap_handle_close_thread_exit("
    "vmar_handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "addr:\x1B[32mzx_vaddr_t\x1B[0m: \x1B[34m0000000000012345\x1B[0m, "
    "size:\x1B[32msize_t\x1B[0m: \x1B[34m1024\x1B[0m, "
    "close_handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1222\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

}  // namespace fidlcat
