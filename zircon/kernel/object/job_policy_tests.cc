// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>

#include <fbl/algorithm.h>

#include "object/job_policy.h"

namespace {

static bool initial_state() {
  BEGIN_TEST;

  auto p = JobPolicy::CreateRootPolicy();

  for (uint32_t pol = 0; pol < ZX_POL_MAX; ++pol) {
    if (pol == ZX_POL_NEW_ANY)
      continue;
    EXPECT_EQ(ZX_POL_ACTION_ALLOW, p.QueryBasicPolicy(pol));
    EXPECT_EQ(ZX_POL_OVERRIDE_ALLOW, p.QueryBasicPolicyOverride(pol));
  }

  EXPECT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_ANY));

  const TimerSlack slack = p.GetTimerSlack();
  EXPECT_TRUE(slack == TimerSlack::none());

  END_TEST;
}

// Verify that AddBasicPolicy prevents "widening" of a deny all policy.
static bool add_basic_policy_no_widening() {
  BEGIN_TEST;

  auto p = JobPolicy::CreateRootPolicy();

  // Start with deny all.
  zx_policy_basic_v2_t policy{ZX_POL_NEW_ANY, ZX_POL_ACTION_DENY, ZX_POL_OVERRIDE_DENY};
  ASSERT_EQ(ZX_OK, p.AddBasicPolicy(ZX_JOB_POL_ABSOLUTE, &policy, 1));
  ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_EVENT));

  // Attempt to allow event creation.
  policy = {ZX_POL_NEW_EVENT, ZX_POL_ACTION_ALLOW, ZX_POL_OVERRIDE_DENY};
  // Fails because mode is ZX_JOB_POL_ABSOLUTE.
  ASSERT_EQ(ZX_ERR_ALREADY_EXISTS, p.AddBasicPolicy(ZX_JOB_POL_ABSOLUTE, &policy, 1));
  // Does not fail because mode is ZX_JOB_POL_RELATIVE.
  ASSERT_EQ(ZX_OK, p.AddBasicPolicy(ZX_JOB_POL_RELATIVE, &policy, 1));

  // However, action is still deny.
  ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_EVENT));
  ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_VMO));

  END_TEST;
}

static bool add_basic_policy_allow_widening() {
  BEGIN_TEST;

  auto p = JobPolicy::CreateRootPolicy();

  // Start with deny all, but allowing override.
  zx_policy_basic_v2_t policy{ZX_POL_NEW_ANY, ZX_POL_ACTION_DENY, ZX_POL_OVERRIDE_ALLOW};
  ASSERT_EQ(ZX_OK, p.AddBasicPolicy(ZX_JOB_POL_ABSOLUTE, &policy, 1));
  ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_EVENT));

  // Allow event creation.
  policy = {ZX_POL_NEW_EVENT, ZX_POL_ACTION_ALLOW, ZX_POL_OVERRIDE_DENY};
  ASSERT_EQ(ZX_OK, p.AddBasicPolicy(ZX_JOB_POL_ABSOLUTE, &policy, 1));
  // Test that it in fact, allows for event, but denies for VMO.
  ASSERT_EQ(ZX_POL_ACTION_ALLOW, p.QueryBasicPolicy(ZX_POL_NEW_EVENT));
  ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_VMO));

  END_TEST;
}

// Verify that AddBasicPolicy prevents "widening" of policy using NEW_ANY.
static bool add_basic_policy_no_widening_with_any() {
  BEGIN_TEST;

  auto p = JobPolicy::CreateRootPolicy();

  // Start with deny event creation.
  zx_policy_basic_v2_t policy{ZX_POL_NEW_EVENT, ZX_POL_ACTION_DENY, ZX_POL_OVERRIDE_DENY};
  ASSERT_EQ(ZX_OK, p.AddBasicPolicy(ZX_JOB_POL_ABSOLUTE, &policy, 1));
  ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_EVENT));

  // Attempt to allow event creation.
  policy = {ZX_POL_NEW_EVENT, ZX_POL_ACTION_ALLOW, ZX_POL_OVERRIDE_DENY};
  // Fails because mode is ZX_JOB_POL_ABSOLUTE.
  ASSERT_EQ(ZX_ERR_ALREADY_EXISTS, p.AddBasicPolicy(ZX_JOB_POL_ABSOLUTE, &policy, 1));
  // Does not fail because mode is ZX_JOB_POL_RELATIVE.
  ASSERT_EQ(ZX_OK, p.AddBasicPolicy(ZX_JOB_POL_RELATIVE, &policy, 1));

  // However, action is still deny.
  ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_EVENT));

  // Attempt to allow any.
  policy = {ZX_POL_NEW_ANY, ZX_POL_ACTION_ALLOW, ZX_POL_OVERRIDE_DENY};
  ASSERT_EQ(ZX_ERR_ALREADY_EXISTS, p.AddBasicPolicy(ZX_JOB_POL_ABSOLUTE, &policy, 1));
  ASSERT_EQ(ZX_OK, p.AddBasicPolicy(ZX_JOB_POL_RELATIVE, &policy, 1));

  // Still deny.
  ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_EVENT));

  END_TEST;
}

static bool add_basic_policy_allow_widening_with_any() {
  BEGIN_TEST;

  auto p = JobPolicy::CreateRootPolicy();

  // Start with deny event creation.
  zx_policy_basic_v2_t policy{ZX_POL_NEW_EVENT, ZX_POL_ACTION_DENY, ZX_POL_OVERRIDE_ALLOW};
  ASSERT_EQ(ZX_OK, p.AddBasicPolicy(ZX_JOB_POL_ABSOLUTE, &policy, 1));
  ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_EVENT));

  // Change it to allow any.
  policy = {ZX_POL_NEW_ANY, ZX_POL_ACTION_ALLOW, ZX_POL_OVERRIDE_DENY};
  ASSERT_EQ(ZX_OK, p.AddBasicPolicy(ZX_JOB_POL_ABSOLUTE, &policy, 1));

  // Verify event can now be created.
  ASSERT_EQ(ZX_POL_ACTION_ALLOW, p.QueryBasicPolicy(ZX_POL_NEW_EVENT));

  END_TEST;
}

static bool add_basic_policy_absolute() {
  BEGIN_TEST;

  auto p = JobPolicy::CreateRootPolicy();
  // TODO(cpu). Don't allow this. It is proably a logic bug in the caller.
  zx_policy_basic_v2_t repeated[2]{{ZX_POL_NEW_ANY, ZX_POL_ACTION_DENY, ZX_POL_OVERRIDE_DENY},
                                   {ZX_POL_NEW_ANY, ZX_POL_ACTION_DENY, ZX_POL_OVERRIDE_DENY}};

  ASSERT_EQ(ZX_OK, p.AddBasicPolicy(ZX_JOB_POL_ABSOLUTE, repeated, 2));
  ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_EVENT));

  zx_policy_basic_v2_t conflicting[2]{{ZX_POL_NEW_ANY, ZX_POL_ACTION_DENY, ZX_POL_OVERRIDE_DENY},
                                      {ZX_POL_NEW_ANY, ZX_POL_ACTION_ALLOW, ZX_POL_OVERRIDE_DENY}};
  ASSERT_EQ(ZX_ERR_ALREADY_EXISTS, p.AddBasicPolicy(ZX_JOB_POL_ABSOLUTE, conflicting, 2));
  ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_VMO));

  END_TEST;
}

static bool add_basic_policy_relative() {
  BEGIN_TEST;

  auto p = JobPolicy::CreateRootPolicy();
  // TODO(cpu). Don't allow this. It is proably a logic bug in the caller.
  zx_policy_basic_v2_t repeated[2]{{ZX_POL_NEW_ANY, ZX_POL_ACTION_DENY, ZX_POL_OVERRIDE_DENY},
                                   {ZX_POL_NEW_ANY, ZX_POL_ACTION_DENY, ZX_POL_OVERRIDE_DENY}};

  ASSERT_EQ(ZX_OK, p.AddBasicPolicy(ZX_JOB_POL_RELATIVE, repeated, 2));
  ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_TIMER));

  zx_policy_basic_v2_t conflicting[2]{{ZX_POL_NEW_ANY, ZX_POL_ACTION_DENY, ZX_POL_OVERRIDE_DENY},
                                      {ZX_POL_NEW_ANY, ZX_POL_ACTION_ALLOW, ZX_POL_OVERRIDE_DENY}};

  ASSERT_EQ(ZX_OK, p.AddBasicPolicy(ZX_JOB_POL_RELATIVE, conflicting, 2));
  ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_FIFO));

  END_TEST;
}

// Test that AddBasicPolicy does not modify JobPolicy when it fails.
static bool add_basic_policy_unmodified_on_error(uint32_t flags) {
  BEGIN_TEST;

  auto p = JobPolicy::CreateRootPolicy();

  zx_policy_basic_v2_t policy[2]{{ZX_POL_NEW_VMO, ZX_POL_ACTION_ALLOW_EXCEPTION, flags},
                                 {ZX_POL_NEW_CHANNEL, ZX_POL_ACTION_KILL, flags}};

  ASSERT_EQ(ZX_OK, p.AddBasicPolicy(ZX_JOB_POL_ABSOLUTE, policy, fbl::count_of(policy)));
  ASSERT_EQ(ZX_POL_ACTION_ALLOW_EXCEPTION, p.QueryBasicPolicy(ZX_POL_NEW_VMO));
  ASSERT_EQ(ZX_POL_ACTION_KILL, p.QueryBasicPolicy(ZX_POL_NEW_CHANNEL));

  const JobPolicy orig = p;

  zx_policy_basic_v2_t new_policy{ZX_POL_NEW_ANY, UINT32_MAX, flags};
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, p.AddBasicPolicy(ZX_JOB_POL_ABSOLUTE, &new_policy, 1));
  ASSERT_TRUE(orig == p);

  if (flags == ZX_POL_OVERRIDE_DENY) {
    new_policy = {ZX_POL_NEW_VMO, ZX_POL_ACTION_ALLOW, flags};
    ASSERT_EQ(ZX_ERR_ALREADY_EXISTS, p.AddBasicPolicy(ZX_JOB_POL_ABSOLUTE, &new_policy, 1));
    ASSERT_TRUE(orig == p);
  }

  END_TEST;
}

static bool add_basic_policy_unmodified_on_error_no_override() {
  return add_basic_policy_unmodified_on_error(ZX_POL_OVERRIDE_DENY);
}

static bool add_basic_policy_unmodified_on_error_with_override() {
  return add_basic_policy_unmodified_on_error(ZX_POL_OVERRIDE_ALLOW);
}

static bool add_basic_policy_deny_any_new(uint32_t flags) {
  BEGIN_TEST;

  auto p = JobPolicy::CreateRootPolicy();
  zx_policy_basic_v2_t policy{ZX_POL_NEW_ANY, ZX_POL_ACTION_DENY, flags};

  ASSERT_EQ(ZX_OK, p.AddBasicPolicy(ZX_JOB_POL_ABSOLUTE, &policy, 1));
  ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_VMO));
  ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_CHANNEL));
  ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_EVENT));
  ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_EVENTPAIR));
  ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_PORT));
  ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_SOCKET));
  ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_FIFO));
  ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_TIMER));
  ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_PROCESS));
  ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_PROFILE));

  ASSERT_EQ(ZX_POL_ACTION_ALLOW, p.QueryBasicPolicy(ZX_POL_BAD_HANDLE));
  ASSERT_EQ(ZX_POL_ACTION_ALLOW, p.QueryBasicPolicy(ZX_POL_WRONG_OBJECT));
  ASSERT_EQ(ZX_POL_ACTION_ALLOW, p.QueryBasicPolicy(ZX_POL_VMAR_WX));
  ASSERT_EQ(ZX_POL_ACTION_ALLOW, p.QueryBasicPolicy(ZX_POL_AMBIENT_MARK_VMO_EXEC));

  END_TEST;
}

static bool add_basic_policy_deny_any_new_no_override() {
  return add_basic_policy_deny_any_new(ZX_POL_OVERRIDE_DENY);
}

static bool add_basic_policy_deny_any_new_with_override() {
  return add_basic_policy_deny_any_new(ZX_POL_OVERRIDE_ALLOW);
}

static bool set_get_timer_slack() {
  BEGIN_TEST;

  auto p = JobPolicy::CreateRootPolicy();

  p.SetTimerSlack({1200, TIMER_SLACK_EARLY});
  ASSERT_EQ(1200, p.GetTimerSlack().amount());
  ASSERT_EQ(TIMER_SLACK_EARLY, p.GetTimerSlack().mode());

  END_TEST;
}

static bool increment_counters() {
  BEGIN_TEST;

  // There's no programmatic interface to read kcounters so there's nothing to assert (aside from
  // not crashing).
  auto p = JobPolicy::CreateRootPolicy();

  for (uint32_t action = 0; action < ZX_POL_ACTION_MAX; ++action) {
    for (uint32_t condition = 0; condition < ZX_POL_MAX; ++condition) {
      p.IncrementCounter(action, condition);
    }
  }

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(job_policy_tests)
UNITTEST("initial_state", initial_state)
UNITTEST("add_basic_policy_no_widening", add_basic_policy_no_widening)
UNITTEST("add_basic_policy_allow_widening", add_basic_policy_allow_widening)
UNITTEST("add_basic_policy_no_widening_with_any", add_basic_policy_no_widening_with_any)
UNITTEST("add_basic_policy_allow_widening_with_any", add_basic_policy_allow_widening_with_any)
UNITTEST("add_basic_policy_absolute", add_basic_policy_absolute)
UNITTEST("add_basic_policy_relative", add_basic_policy_relative)
UNITTEST("add_basic_policy_unmodified_on_error_no_override",
         add_basic_policy_unmodified_on_error_no_override)
UNITTEST("add_basic_policy_unmodified_on_error_with_override",
         add_basic_policy_unmodified_on_error_with_override)
UNITTEST("add_basic_policy_deny_any_new_no_override", add_basic_policy_deny_any_new_no_override)
UNITTEST("add_basic_policy_deny_any_new_with_override", add_basic_policy_deny_any_new_with_override)
UNITTEST("set_get_timer_slack", set_get_timer_slack)
UNITTEST("increment_counters", increment_counters)
UNITTEST_END_TESTCASE(job_policy_tests, "job_policy", "JobPolicy tests")
