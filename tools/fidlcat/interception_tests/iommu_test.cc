// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls/iommu.h>

#include <gtest/gtest.h>

#include "tools/fidlcat/interception_tests/interception_workflow_test.h"

namespace fidlcat {

// zx_iommu_create tests.

std::unique_ptr<SystemCallTest> ZxIommuCreate(int64_t result, std::string_view result_name,
                                              zx_handle_t resource, uint32_t type, const void* desc,
                                              size_t desc_size, zx_handle_t* out) {
  auto value = std::make_unique<SystemCallTest>("zx_iommu_create", result, result_name);
  value->AddInput(resource);
  value->AddInput(type);
  value->AddInput(reinterpret_cast<uint64_t>(desc));
  value->AddInput(desc_size);
  value->AddInput(reinterpret_cast<uint64_t>(out));
  return value;
}

#define IOMMU_CREATE_DUMMY_DISPLAY_TEST_CONTENT(result, expected)                            \
  zx_handle_t handle_out = kHandleOut;                                                       \
  PerformDisplayTest(                                                                        \
      "$plt(zx_iommu_create)",                                                               \
      ZxIommuCreate(result, #result, kHandle, ZX_IOMMU_TYPE_DUMMY, nullptr, 0, &handle_out), \
      expected);

#define IOMMU_CREATE_DUMMY_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                  \
    IOMMU_CREATE_DUMMY_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                            \
  TEST_F(InterceptionWorkflowTestArm, name) {                  \
    IOMMU_CREATE_DUMMY_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

IOMMU_CREATE_DUMMY_DISPLAY_TEST(
    ZxIommuCreateDummy, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_iommu_create("
    "resource: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "type: \x1B[32mzx_iommu_type_t\x1B[0m = \x1B[31mZX_IOMMU_TYPE_DUMMY\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (out: \x1B[32mhandle\x1B[0m = \x1B[31mbde90caf\x1B[0m)\n");

#define IOMMU_CREATE_INTEL_DISPLAY_TEST_CONTENT(result, expected)                        \
  zx_iommu_desc_intel_t desc = {.register_base = 0x1234,                                 \
                                .pci_segment = 100,                                      \
                                .whole_segment = true,                                   \
                                .scope_bytes = 8,                                        \
                                .reserved_memory_bytes = 1024};                          \
  zx_handle_t handle_out = kHandleOut;                                                   \
  PerformDisplayTest("$plt(zx_iommu_create)",                                            \
                     ZxIommuCreate(result, #result, kHandle, ZX_IOMMU_TYPE_INTEL, &desc, \
                                   sizeof(desc), &handle_out),                           \
                     expected);

#define IOMMU_CREATE_INTEL_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                  \
    IOMMU_CREATE_INTEL_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                            \
  TEST_F(InterceptionWorkflowTestArm, name) {                  \
    IOMMU_CREATE_INTEL_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

IOMMU_CREATE_INTEL_DISPLAY_TEST(
    ZxIommuCreateIntel, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_iommu_create("
    "resource: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "type: \x1B[32mzx_iommu_type_t\x1B[0m = \x1B[31mZX_IOMMU_TYPE_INTEL\x1B[0m)\n"
    "  desc: \x1B[32mzx_iommu_desc_intel_t\x1B[0m = {\n"
    "    register_base: \x1B[32mzx_paddr_t\x1B[0m = \x1B[34m0000000000001234\x1B[0m\n"
    "    pci_segment: \x1B[32muint16\x1B[0m = \x1B[34m100\x1B[0m\n"
    "    whole_segment: \x1B[32mbool\x1B[0m = \x1B[34mtrue\x1B[0m\n"
    "    scope_bytes: \x1B[32muint8\x1B[0m = \x1B[34m8\x1B[0m\n"
    "    reserved_memory_bytes: \x1B[32muint16\x1B[0m = \x1B[34m1024\x1B[0m\n"
    "  }\n"
    "  -> \x1B[32mZX_OK\x1B[0m (out: \x1B[32mhandle\x1B[0m = \x1B[31mbde90caf\x1B[0m)\n");

}  // namespace fidlcat
