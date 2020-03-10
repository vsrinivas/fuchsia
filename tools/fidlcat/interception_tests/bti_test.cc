// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"

namespace fidlcat {

// zx_bti_create tests.

std::unique_ptr<SystemCallTest> ZxBtiCreate(int64_t result, std::string_view result_name,
                                            zx_handle_t iommu, uint32_t options, uint64_t bti_id,
                                            zx_handle_t* out) {
  auto value = std::make_unique<SystemCallTest>("zx_bti_create", result, result_name);
  value->AddInput(iommu);
  value->AddInput(options);
  value->AddInput(bti_id);
  value->AddInput(reinterpret_cast<uint64_t>(out));
  return value;
}

#define BTI_CREATE_DISPLAY_TEST_CONTENT(result, expected)                                       \
  zx_handle_t out = kHandleOut;                                                                 \
  PerformDisplayTest("$plt(zx_bti_create)", ZxBtiCreate(result, #result, kHandle, 0, 10, &out), \
                     expected);

#define BTI_CREATE_DISPLAY_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { BTI_CREATE_DISPLAY_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { BTI_CREATE_DISPLAY_TEST_CONTENT(errno, expected); }

BTI_CREATE_DISPLAY_TEST(
    ZxBtiCreate, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_bti_create("
    "iommu:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m, "
    "bti_id:\x1B[32muint64\x1B[0m: \x1B[34m10\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (out:\x1B[32mhandle\x1B[0m: \x1B[31mbde90caf\x1B[0m)\n");

// zx_bti_pin tests.

std::unique_ptr<SystemCallTest> ZxBtiPin(int64_t result, std::string_view result_name,
                                         zx_handle_t handle, uint32_t options, zx_handle_t vmo,
                                         uint64_t offset, uint64_t size, zx_paddr_t* addrs,
                                         size_t addrs_count, zx_handle_t* pmt) {
  auto value = std::make_unique<SystemCallTest>("zx_bti_pin", result, result_name);
  value->AddInput(handle);
  value->AddInput(options);
  value->AddInput(vmo);
  value->AddInput(offset);
  value->AddInput(size);
  value->AddInput(reinterpret_cast<uint64_t>(addrs));
  value->AddInput(addrs_count);
  value->AddInput(reinterpret_cast<uint64_t>(pmt));
  return value;
}

#define BTI_PIN_DISPLAY_TEST_CONTENT(result, expected)                                          \
  std::vector<zx_paddr_t> addrs = {0x1234, 0x2345, 0x3456};                                     \
  zx_handle_t pmt = kHandleOut;                                                                 \
  PerformDisplayTest("$plt(zx_bti_pin)",                                                        \
                     ZxBtiPin(result, #result, kHandle, ZX_BTI_PERM_READ | ZX_BTI_PERM_EXECUTE, \
                              kHandle2, 1000, 1024, addrs.data(), addrs.size(), &pmt),          \
                     expected);

#define BTI_PIN_DISPLAY_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { BTI_PIN_DISPLAY_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { BTI_PIN_DISPLAY_TEST_CONTENT(errno, expected); }

BTI_PIN_DISPLAY_TEST(
    ZxBtiPin, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_bti_pin("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "options:\x1B[32mzx_bti_perm_t\x1B[0m: \x1B[34mZX_BTI_PERM_READ | ZX_BTI_PERM_EXECUTE\x1B[0m, "
    "vmo:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1222\x1B[0m, "
    "offset:\x1B[32muint64\x1B[0m: \x1B[34m1000\x1B[0m, "
    "size:\x1B[32muint64\x1B[0m: \x1B[34m1024\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (pmt:\x1B[32mhandle\x1B[0m: \x1B[31mbde90caf\x1B[0m)\n"
    "      addrs:\x1B[32mzx_paddr_t\x1B[0m: "
    "\x1B[34m0000000000001234\x1B[0m, \x1B[34m0000000000002345\x1B[0m, "
    "\x1B[34m0000000000003456\x1B[0m\n");

// zx_bti_release_quarantine tests.

std::unique_ptr<SystemCallTest> ZxBtiReleaseQuarantine(int64_t result, std::string_view result_name,
                                                       zx_handle_t handle) {
  auto value = std::make_unique<SystemCallTest>("zx_bti_release_quarantine", result, result_name);
  value->AddInput(handle);
  return value;
}

#define BTI_RELEASE_QUARANTINE_DISPLAY_TEST_CONTENT(result, expected) \
  PerformDisplayTest("$plt(zx_bti_release_quarantine)",               \
                     ZxBtiReleaseQuarantine(result, #result, kHandle), expected);

#define BTI_RELEASE_QUARANTINE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                      \
    BTI_RELEASE_QUARANTINE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                \
  TEST_F(InterceptionWorkflowTestArm, name) {                      \
    BTI_RELEASE_QUARANTINE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

BTI_RELEASE_QUARANTINE_DISPLAY_TEST(
    ZxBtiReleaseQuarantine, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_bti_release_quarantine(handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

}  // namespace fidlcat
