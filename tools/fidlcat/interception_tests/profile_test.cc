// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls/profile.h>

#include <gtest/gtest.h>

#include "tools/fidlcat/interception_tests/interception_workflow_test.h"

namespace fidlcat {

// zx_profile_create tests.

std::unique_ptr<SystemCallTest> ZxProfileCreate(int64_t result, std::string_view result_name,
                                                zx_handle_t root_job, uint32_t options,
                                                const zx_profile_info_t* profile,
                                                zx_handle_t* out) {
  auto value = std::make_unique<SystemCallTest>("zx_profile_create", result, result_name);
  value->AddInput(root_job);
  value->AddInput(options);
  value->AddInput(reinterpret_cast<uint64_t>(profile));
  value->AddInput(reinterpret_cast<uint64_t>(out));
  return value;
}

#define PROFILE_CREATE_DISPLAY_TEST_CONTENT(result, expected)                    \
  zx_profile_info_t profile;                                                     \
  profile.flags = ZX_PROFILE_INFO_FLAG_PRIORITY | ZX_PROFILE_INFO_FLAG_CPU_MASK; \
  profile.priority = -1;                                                         \
  memset(&profile.cpu_affinity_mask, 0, sizeof(profile.cpu_affinity_mask));      \
  profile.cpu_affinity_mask.mask[0] = 0xe;                                       \
  zx_handle_t out = kHandleOut;                                                  \
  PerformDisplayTest("$plt(zx_profile_create)",                                  \
                     ZxProfileCreate(result, #result, kHandle, 0, &profile, &out), expected);

#define PROFILE_CREATE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {              \
    PROFILE_CREATE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                        \
  TEST_F(InterceptionWorkflowTestArm, name) {              \
    PROFILE_CREATE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

PROFILE_CREATE_DISPLAY_TEST(
    ZxProfileCreate, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_profile_create("
    "root_job: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "options: \x1B[32muint32\x1B[0m = \x1B[34m0\x1B[0m)\n"
    "  info: \x1B[32mzx_profile_info_t\x1B[0m = {\n"
    "    flags: \x1B[32mzx.profile_info_flags\x1B[0m = "
    "\x1B[34mZX_PROFILE_INFO_FLAG_PRIORITY | ZX_PROFILE_INFO_FLAG_CPU_MASK\x1B[0m\n"
    "    priority: \x1B[32mint32\x1B[0m = \x1B[34m-1\x1B[0m\n"
    "    cpu_affinity_mask: \x1B[32mzx_cpu_set_t\x1B[0m = {\n"
    "      mask: vector<\x1B[32muint64\x1B[0m> = [\n"
    "        \x1B[34m000000000000000e\x1B[0m, \x1B[34m0000000000000000\x1B[0m, "
    "\x1B[34m0000000000000000\x1B[0m, \x1B[34m0000000000000000\x1B[0m, "
    "\x1B[34m0000000000000000\x1B[0m, \x1B[34m0000000000000000\x1B[0m\n"
    "        \x1B[34m0000000000000000\x1B[0m, \x1B[34m0000000000000000\x1B[0m\n"
    "      ]\n"
    "    }\n"
    "  }\n"
    "  -> \x1B[32mZX_OK\x1B[0m (out: \x1B[32mhandle\x1B[0m = \x1B[31mbde90caf\x1B[0m)\n");

}  // namespace fidlcat
