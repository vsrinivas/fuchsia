// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"

namespace fidlcat {

// zx_resource_create tests.

std::unique_ptr<SystemCallTest> ZxResourceCreate(int64_t result, std::string_view result_name,
                                                 zx_handle_t parent_rsrc, uint32_t options,
                                                 uint64_t base, size_t size, const char* name,
                                                 size_t name_size, zx_handle_t* resource_out) {
  auto value = std::make_unique<SystemCallTest>("zx_resource_create", result, result_name);
  value->AddInput(parent_rsrc);
  value->AddInput(options);
  value->AddInput(base);
  value->AddInput(size);
  value->AddInput(reinterpret_cast<uint64_t>(name));
  value->AddInput(name_size);
  value->AddInput(reinterpret_cast<uint64_t>(resource_out));
  return value;
}

#define RESOURCE_CREATE_DISPLAY_TEST_CONTENT(result, expected)                                 \
  std::string name = "My resource";                                                            \
  zx_handle_t resource_out = kHandleOut;                                                       \
  PerformDisplayTest("$plt(zx_resource_create)",                                               \
                     ZxResourceCreate(result, #result, kHandle, ZX_RSRC_KIND_ROOT, 1000, 1024, \
                                      name.c_str(), name.size(), &resource_out),               \
                     expected);

#define RESOURCE_CREATE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {               \
    RESOURCE_CREATE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                         \
  TEST_F(InterceptionWorkflowTestArm, name) {               \
    RESOURCE_CREATE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

RESOURCE_CREATE_DISPLAY_TEST(
    ZxResourceCreate, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_resource_create("
    "parent_rsrc:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "options:\x1B[32mzx_rsrc_kind_t\x1B[0m: \x1B[34mZX_RSRC_KIND_ROOT\x1B[0m, "
    "base:\x1B[32muint64\x1B[0m: \x1B[34m1000\x1B[0m, "
    "size:\x1B[32msize_t\x1B[0m: \x1B[34m1024\x1B[0m, "
    "name:\x1B[32mstring\x1B[0m: \x1B[31m\"My resource\"\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (resource_out:\x1B[32mhandle\x1B[0m: \x1B[31mbde90caf\x1B[0m)\n");

}  // namespace fidlcat
