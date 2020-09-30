// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls/policy.h>

#include <gtest/gtest.h>

#include "tools/fidlcat/interception_tests/interception_workflow_test.h"

namespace fidlcat {

// zx_job_create tests.

std::unique_ptr<SystemCallTest> ZxJobCreate(int64_t result, std::string_view result_name,
                                            zx_handle_t parent_job, uint32_t options,
                                            zx_handle_t* out) {
  auto value = std::make_unique<SystemCallTest>("zx_job_create", result, result_name);
  value->AddInput(parent_job);
  value->AddInput(options);
  value->AddInput(reinterpret_cast<uint64_t>(out));
  return value;
}

#define JOB_CREATE_DISPLAY_TEST_CONTENT(result, expected)                                   \
  zx_handle_t out = kHandleOut;                                                             \
  PerformDisplayTest("$plt(zx_job_create)", ZxJobCreate(result, #result, kHandle, 0, &out), \
                     expected);

#define JOB_CREATE_DISPLAY_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { JOB_CREATE_DISPLAY_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { JOB_CREATE_DISPLAY_TEST_CONTENT(errno, expected); }

JOB_CREATE_DISPLAY_TEST(
    ZxJobCreate, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_job_create("
    "parent_job: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "options: \x1B[32muint32\x1B[0m = \x1B[34m0\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (out: \x1B[32mhandle\x1B[0m = \x1B[31mbde90caf\x1B[0m)\n");

// zx_job_set_policy tests.

std::unique_ptr<SystemCallTest> ZxJobSetPolicy(int64_t result, std::string_view result_name,
                                               zx_handle_t handle, uint32_t options, uint32_t topic,
                                               const void* policy, uint32_t count) {
  auto value = std::make_unique<SystemCallTest>("zx_job_set_policy", result, result_name);
  value->AddInput(handle);
  value->AddInput(options);
  value->AddInput(topic);
  value->AddInput(reinterpret_cast<uint64_t>(policy));
  value->AddInput(count);
  return value;
}

#define JOB_SET_POLICY_BASIC_DISPLAY_TEST_CONTENT(result, expected)                                \
  std::vector<zx_policy_basic_t> policy(2);                                                        \
  policy[0] = {.condition = ZX_POL_VMAR_WX, .policy = ZX_POL_ACTION_ALLOW};                        \
  policy[1] = {.condition = ZX_POL_NEW_VMO, .policy = ZX_POL_ACTION_DENY};                         \
  PerformDisplayTest(                                                                              \
      "$plt(zx_job_set_policy)",                                                                   \
      ZxJobSetPolicy(result, #result, kHandle, 0, ZX_JOB_POL_BASIC, policy.data(), policy.size()), \
      expected);

#define JOB_SET_POLICY_BASIC_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                    \
    JOB_SET_POLICY_BASIC_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                              \
  TEST_F(InterceptionWorkflowTestArm, name) {                    \
    JOB_SET_POLICY_BASIC_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

JOB_SET_POLICY_BASIC_DISPLAY_TEST(
    ZxJobSetPolicyBasic, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_job_set_policy("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "options: \x1B[32muint32\x1B[0m = \x1B[34m0\x1B[0m, "
    "topic: \x1B[32mzx_policy_topic_t\x1B[0m = \x1B[34mZX_JOB_POL_BASIC\x1B[0m)\n"
    "  policy: vector<\x1B[32mzx_policy_basic_t\x1B[0m> =  [\n"
    "    {\n"
    "      condition: \x1B[32mzx_policy_condition_t\x1B[0m = \x1B[34mZX_POL_VMAR_WX\x1B[0m\n"
    "      policy: \x1B[32mzx_policy_action_t\x1B[0m = \x1B[34mZX_POL_ACTION_ALLOW\x1B[0m\n"
    "    },\n"
    "    {\n"
    "      condition: \x1B[32mzx_policy_condition_t\x1B[0m = \x1B[34mZX_POL_NEW_VMO\x1B[0m\n"
    "      policy: \x1B[32mzx_policy_action_t\x1B[0m = \x1B[34mZX_POL_ACTION_DENY\x1B[0m\n"
    "    }\n"
    "  ]\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

#define JOB_SET_POLICY_TIMER_SLACK_DISPLAY_TEST_CONTENT(result, expected)                     \
  zx_policy_timer_slack_t policy = {.min_slack = 100, .default_mode = ZX_TIMER_SLACK_CENTER}; \
  PerformDisplayTest(                                                                         \
      "$plt(zx_job_set_policy)",                                                              \
      ZxJobSetPolicy(result, #result, kHandle, 0, ZX_JOB_POL_TIMER_SLACK, &policy, 1), expected);

#define JOB_SET_POLICY_TIMER_SLACK_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                          \
    JOB_SET_POLICY_TIMER_SLACK_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                    \
  TEST_F(InterceptionWorkflowTestArm, name) {                          \
    JOB_SET_POLICY_TIMER_SLACK_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

JOB_SET_POLICY_TIMER_SLACK_DISPLAY_TEST(
    ZxJobSetPolicyTimerSlack, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_job_set_policy("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m,"
    " options: \x1B[32muint32\x1B[0m = \x1B[34m0\x1B[0m, "
    "topic: \x1B[32mzx_policy_topic_t\x1B[0m = \x1B[34mZX_JOB_POL_TIMER_SLACK\x1B[0m)\n"
    "  policy: \x1B[32mzx_policy_timer_slack_t\x1B[0m = {\n"
    "    min_slack: \x1B[32mduration\x1B[0m = \x1B[34m100 nano seconds\x1B[0m\n"
    "    default_mode: \x1B[32mzx_timer_option_t\x1B[0m = \x1B[34mZX_TIMER_SLACK_CENTER\x1B[0m\n"
    "  }\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

}  // namespace fidlcat
