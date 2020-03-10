// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"

namespace fidlcat {

// zx_pager_create tests.

std::unique_ptr<SystemCallTest> ZxPagerCreate(int64_t result, std::string_view result_name,
                                              uint32_t options, zx_handle_t* out) {
  auto value = std::make_unique<SystemCallTest>("zx_pager_create", result, result_name);
  value->AddInput(options);
  value->AddInput(reinterpret_cast<uint64_t>(out));
  return value;
}

#define PAGER_CREATE_DISPLAY_TEST_CONTENT(result, expected) \
  zx_handle_t out = kHandleOut;                             \
  PerformDisplayTest("$plt(zx_pager_create)", ZxPagerCreate(result, #result, 0, &out), expected);

#define PAGER_CREATE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {            \
    PAGER_CREATE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                      \
  TEST_F(InterceptionWorkflowTestArm, name) { PAGER_CREATE_DISPLAY_TEST_CONTENT(errno, expected); }

PAGER_CREATE_DISPLAY_TEST(
    ZxPagerCreate, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_pager_create(options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (out:\x1B[32mhandle\x1B[0m: \x1B[31mbde90caf\x1B[0m)\n");

// zx_pager_create_vmo tests.

std::unique_ptr<SystemCallTest> ZxPagerCreateVmo(int64_t result, std::string_view result_name,
                                                 zx_handle_t pager, uint32_t options,
                                                 zx_handle_t port, uint64_t key, uint64_t size,
                                                 zx_handle_t* out) {
  auto value = std::make_unique<SystemCallTest>("zx_pager_create_vmo", result, result_name);
  value->AddInput(pager);
  value->AddInput(options);
  value->AddInput(port);
  value->AddInput(key);
  value->AddInput(size);
  value->AddInput(reinterpret_cast<uint64_t>(out));
  return value;
}

#define PAGER_CREATE_VMO_DISPLAY_TEST_CONTENT(result, expected)                              \
  zx_handle_t out = kHandleOut;                                                              \
  PerformDisplayTest("$plt(zx_pager_create_vmo)",                                            \
                     ZxPagerCreateVmo(result, #result, kHandle, 0, kPort, kKey, 1024, &out), \
                     expected);

#define PAGER_CREATE_VMO_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                \
    PAGER_CREATE_VMO_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                          \
  TEST_F(InterceptionWorkflowTestArm, name) {                \
    PAGER_CREATE_VMO_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

PAGER_CREATE_VMO_DISPLAY_TEST(
    ZxPagerCreateVmo, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_pager_create_vmo("
    "pager:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m, "
    "port:\x1B[32mhandle\x1B[0m: \x1B[31mdf0b2ec1\x1B[0m, "
    "key:\x1B[32muint64\x1B[0m: \x1B[34m1234\x1B[0m, "
    "size:\x1B[32muint64\x1B[0m: \x1B[34m1024\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (out:\x1B[32mhandle\x1B[0m: \x1B[31mbde90caf\x1B[0m)\n");

// zx_pager_detach_vmo tests.

std::unique_ptr<SystemCallTest> ZxPagerDetachVmo(int64_t result, std::string_view result_name,
                                                 zx_handle_t pager, zx_handle_t vmo) {
  auto value = std::make_unique<SystemCallTest>("zx_pager_detach_vmo", result, result_name);
  value->AddInput(pager);
  value->AddInput(vmo);
  return value;
}

#define PAGER_DETACH_VMO_DISPLAY_TEST_CONTENT(result, expected) \
  PerformDisplayTest("$plt(zx_pager_detach_vmo)",               \
                     ZxPagerDetachVmo(result, #result, kHandle, kHandle2), expected);

#define PAGER_DETACH_VMO_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                \
    PAGER_DETACH_VMO_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                          \
  TEST_F(InterceptionWorkflowTestArm, name) {                \
    PAGER_DETACH_VMO_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

PAGER_DETACH_VMO_DISPLAY_TEST(ZxPagerDetachVmo, ZX_OK,
                              "\n"
                              "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                              "zx_pager_detach_vmo("
                              "pager:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                              "vmo:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1222\x1B[0m)\n"
                              "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_pager_supply_pages tests.

std::unique_ptr<SystemCallTest> ZxPagerSupplyPages(int64_t result, std::string_view result_name,
                                                   zx_handle_t pager, zx_handle_t pager_vmo,
                                                   uint64_t offset, uint64_t length,
                                                   zx_handle_t aux_vmo, uint64_t aux_offset) {
  auto value = std::make_unique<SystemCallTest>("zx_pager_supply_pages", result, result_name);
  value->AddInput(pager);
  value->AddInput(pager_vmo);
  value->AddInput(offset);
  value->AddInput(length);
  value->AddInput(aux_vmo);
  value->AddInput(aux_offset);
  return value;
}

#define PAGER_SUPPLY_PAGES_DISPLAY_TEST_CONTENT(result, expected)                         \
  PerformDisplayTest(                                                                     \
      "$plt(zx_pager_supply_pages)",                                                      \
      ZxPagerSupplyPages(result, #result, kHandle, kHandle2, 1000, 1024, kHandle3, 2000), \
      expected);

#define PAGER_SUPPLY_PAGES_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                  \
    PAGER_SUPPLY_PAGES_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                            \
  TEST_F(InterceptionWorkflowTestArm, name) {                  \
    PAGER_SUPPLY_PAGES_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

PAGER_SUPPLY_PAGES_DISPLAY_TEST(ZxPagerSupplyPages, ZX_OK,
                                "\n"
                                "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                                "zx_pager_supply_pages("
                                "pager:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                                "pager_vmo:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1222\x1B[0m, "
                                "offset:\x1B[32muint64\x1B[0m: \x1B[34m1000\x1B[0m, "
                                "length:\x1B[32muint64\x1B[0m: \x1B[34m1024\x1B[0m, "
                                "aux_vmo:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1333\x1B[0m, "
                                "aux_offset:\x1B[32muint64\x1B[0m: \x1B[34m2000\x1B[0m)\n"
                                "  -> \x1B[32mZX_OK\x1B[0m\n");

}  // namespace fidlcat
