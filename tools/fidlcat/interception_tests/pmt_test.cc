// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"

namespace fidlcat {

// zx_pmt_unpin tests.

std::unique_ptr<SystemCallTest> ZxPmtUnpin(int64_t result, std::string_view result_name,
                                           zx_handle_t handle) {
  auto value = std::make_unique<SystemCallTest>("zx_pmt_unpin", result, result_name);
  value->AddInput(handle);
  return value;
}

#define PMT_UNPIN_DISPLAY_TEST_CONTENT(result, expected) \
  PerformDisplayTest("$plt(zx_pmt_unpin)", ZxPmtUnpin(result, #result, kHandle), expected);

#define PMT_UNPIN_DISPLAY_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { PMT_UNPIN_DISPLAY_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { PMT_UNPIN_DISPLAY_TEST_CONTENT(errno, expected); }

PMT_UNPIN_DISPLAY_TEST(ZxPmtUnpin, ZX_OK,
                       "\n"
                       "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                       "zx_pmt_unpin(handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m)\n"
                       "  -> \x1B[32mZX_OK\x1B[0m\n");

}  // namespace fidlcat
