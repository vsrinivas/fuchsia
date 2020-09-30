// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls/smc.h>

#include <gtest/gtest.h>

#include "tools/fidlcat/interception_tests/interception_workflow_test.h"

namespace fidlcat {

// zx_smc_call tests.

std::unique_ptr<SystemCallTest> ZxSmcCall(int64_t result, std::string_view result_name,
                                          zx_handle_t handle, const zx_smc_parameters_t* parameters,
                                          zx_smc_result_t* out_smc_result) {
  auto value = std::make_unique<SystemCallTest>("zx_smc_call", result, result_name);
  value->AddInput(handle);
  value->AddInput(reinterpret_cast<uint64_t>(parameters));
  value->AddInput(reinterpret_cast<uint64_t>(out_smc_result));
  return value;
}

#define SMC_CALL_DISPLAY_TEST_CONTENT(result, expected)                                     \
  zx_smc_parameters_t parameters = {.func_id = 1,                                           \
                                    .arg1 = 2,                                              \
                                    .arg2 = 3,                                              \
                                    .arg3 = 4,                                              \
                                    .arg4 = 5,                                              \
                                    .arg5 = 6,                                              \
                                    .arg6 = 7,                                              \
                                    .client_id = 8,                                         \
                                    .secure_os_id = 9};                                     \
  zx_smc_result_t out_smc_result = {.arg0 = 1, .arg1 = 2, .arg2 = 3, .arg3 = 3, .arg6 = 4}; \
  PerformDisplayTest("$plt(zx_smc_call)",                                                   \
                     ZxSmcCall(result, #result, kHandle, &parameters, &out_smc_result), expected);

#define SMC_CALL_DISPLAY_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { SMC_CALL_DISPLAY_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { SMC_CALL_DISPLAY_TEST_CONTENT(errno, expected); }

SMC_CALL_DISPLAY_TEST(ZxSmcCall, ZX_OK,
                      "\n"
                      "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                      "zx_smc_call(handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m)\n"
                      "  parameters: \x1B[32mzx_smc_parameters_t\x1B[0m = {\n"
                      "    func_id: \x1B[32muint32\x1B[0m = \x1B[34m1\x1B[0m\n"
                      "    arg1: \x1B[32muint64\x1B[0m = \x1B[34m2\x1B[0m\n"
                      "    arg2: \x1B[32muint64\x1B[0m = \x1B[34m3\x1B[0m\n"
                      "    arg3: \x1B[32muint64\x1B[0m = \x1B[34m4\x1B[0m\n"
                      "    arg4: \x1B[32muint64\x1B[0m = \x1B[34m5\x1B[0m\n"
                      "    arg5: \x1B[32muint64\x1B[0m = \x1B[34m6\x1B[0m\n"
                      "    arg6: \x1B[32muint64\x1B[0m = \x1B[34m7\x1B[0m\n"
                      "    client_id: \x1B[32muint16\x1B[0m = \x1B[34m8\x1B[0m\n"
                      "    secure_os_id: \x1B[32muint16\x1B[0m = \x1B[34m9\x1B[0m\n"
                      "  }\n"
                      "  -> \x1B[32mZX_OK\x1B[0m\n"
                      "    out_smc_result: \x1B[32mzx_smc_result_t\x1B[0m = { "
                      "arg0: \x1B[32muint64\x1B[0m = \x1B[34m1\x1B[0m, "
                      "arg1: \x1B[32muint64\x1B[0m = \x1B[34m2\x1B[0m, "
                      "arg2: \x1B[32muint64\x1B[0m = \x1B[34m3\x1B[0m, "
                      "arg3: \x1B[32muint64\x1B[0m = \x1B[34m3\x1B[0m, "
                      "arg6: \x1B[32muint64\x1B[0m = \x1B[34m4\x1B[0m "
                      "}\n");

}  // namespace fidlcat
