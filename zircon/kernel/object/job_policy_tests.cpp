// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/job_policy.h>

#include <fbl/algorithm.h>
#include <lib/unittest/unittest.h>

namespace {

static bool initial_state() {
    BEGIN_TEST;

    JobPolicy p;
    for (uint32_t pol = 0; pol < ZX_POL_MAX; ++pol) {
        EXPECT_EQ(ZX_POL_ACTION_ALLOW, p.QueryBasicPolicy(pol), "");
    }
    const TimerSlack slack = p.GetTimerSlack();
    EXPECT_TRUE(slack == TimerSlack::none(), "");

    END_TEST;
}

// Verify that AddBasicPolicy prevents "widening" of a deny all policy.
static bool add_basic_policy_no_widening() {
    BEGIN_TEST;

    JobPolicy p;

    // Start with deny all.
    zx_policy_basic_t policy{ZX_POL_NEW_ANY, ZX_POL_ACTION_DENY};
    ASSERT_EQ(ZX_OK, p.AddBasicPolicy(ZX_JOB_POL_ABSOLUTE, &policy, 1), "");
    ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_EVENT), "");

    // Attempt to allow event creation.
    policy = {ZX_POL_NEW_EVENT, ZX_POL_ACTION_ALLOW};
    // Fails because mode is ZX_JOB_POL_ABSOLUTE.
    ASSERT_EQ(ZX_ERR_ALREADY_EXISTS, p.AddBasicPolicy(ZX_JOB_POL_ABSOLUTE, &policy, 1), "");
    // Does not fail because mode is ZX_JOB_POL_RELATIVE.
    ASSERT_EQ(ZX_OK, p.AddBasicPolicy(ZX_JOB_POL_RELATIVE, &policy, 1), "");

    // However, action is still deny.
    ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_EVENT), "");
    ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_VMO), "");

    END_TEST;
}

// Verify that AddBasicPolicy prevents "widening" of policy using NEW_ANY.
static bool add_basic_policy_no_widening_with_any() {
    BEGIN_TEST;

    JobPolicy p;

    // Start with deny event creation.
    zx_policy_basic_t policy{ZX_POL_NEW_EVENT, ZX_POL_ACTION_DENY};
    ASSERT_EQ(ZX_OK, p.AddBasicPolicy(ZX_JOB_POL_ABSOLUTE, &policy, 1), "");
    ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_EVENT), "");

    // Attempt to allow event creation.
    policy = {ZX_POL_NEW_EVENT, ZX_POL_ACTION_ALLOW};
    // Fails because mode is ZX_JOB_POL_ABSOLUTE.
    ASSERT_EQ(ZX_ERR_ALREADY_EXISTS, p.AddBasicPolicy(ZX_JOB_POL_ABSOLUTE, &policy, 1), "");
    // Does not fail because mode is ZX_JOB_POL_RELATIVE.
    ASSERT_EQ(ZX_OK, p.AddBasicPolicy(ZX_JOB_POL_RELATIVE, &policy, 1), "");

    // However, action is still deny.
    ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_EVENT), "");

    // Attempt to allow any.
    policy = {ZX_POL_NEW_ANY, ZX_POL_ACTION_ALLOW};
    ASSERT_EQ(ZX_ERR_ALREADY_EXISTS, p.AddBasicPolicy(ZX_JOB_POL_ABSOLUTE, &policy, 1), "");
    ASSERT_EQ(ZX_OK, p.AddBasicPolicy(ZX_JOB_POL_RELATIVE, &policy, 1), "");

    // Still deny.
    ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_EVENT), "");

    END_TEST;
}

static bool add_basic_policy_absolute() {
    BEGIN_TEST;

    JobPolicy p;
    zx_policy_basic_t repeated[2]{{ZX_POL_NEW_ANY, ZX_POL_ACTION_DENY},
                                  {ZX_POL_NEW_ANY, ZX_POL_ACTION_DENY}};
    ASSERT_EQ(ZX_OK, p.AddBasicPolicy(ZX_JOB_POL_ABSOLUTE, repeated, 2), "");
    ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_EVENT), "");

    zx_policy_basic_t conflicting[2]{{ZX_POL_NEW_ANY, ZX_POL_ACTION_DENY},
                                     {ZX_POL_NEW_ANY, ZX_POL_ACTION_ALLOW}};
    ASSERT_EQ(ZX_ERR_ALREADY_EXISTS, p.AddBasicPolicy(ZX_JOB_POL_ABSOLUTE, conflicting, 2), "");
    ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_VMO), "");

    END_TEST;
}

static bool add_basic_policy_relative() {
    BEGIN_TEST;

    JobPolicy p;
    zx_policy_basic_t repeated[2]{{ZX_POL_NEW_ANY, ZX_POL_ACTION_DENY},
                                  {ZX_POL_NEW_ANY, ZX_POL_ACTION_DENY}};
    ASSERT_EQ(ZX_OK, p.AddBasicPolicy(ZX_JOB_POL_RELATIVE, repeated, 2), "");
    ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_TIMER), "");

    zx_policy_basic_t conflicting[2]{{ZX_POL_NEW_ANY, ZX_POL_ACTION_DENY},
                                     {ZX_POL_NEW_ANY, ZX_POL_ACTION_ALLOW}};
    ASSERT_EQ(ZX_OK, p.AddBasicPolicy(ZX_JOB_POL_RELATIVE, conflicting, 2), "");
    ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_FIFO), "");

    END_TEST;
}

// Test that AddBasicPolicy does not modify JobPolicy when it fails.
static bool add_basic_policy_unmodified_on_error() {
    BEGIN_TEST;

    JobPolicy p;
    zx_policy_basic_t policy[2]{{ZX_POL_NEW_VMO,
                                 ZX_POL_ACTION_ALLOW | ZX_POL_ACTION_EXCEPTION},
                                {ZX_POL_NEW_CHANNEL,
                                 ZX_POL_ACTION_KILL}};
    ASSERT_EQ(ZX_OK, p.AddBasicPolicy(ZX_JOB_POL_ABSOLUTE, policy, fbl::count_of(policy)), "");
    ASSERT_EQ(ZX_POL_ACTION_ALLOW | ZX_POL_ACTION_EXCEPTION,
              p.QueryBasicPolicy(ZX_POL_NEW_VMO), "");
    ASSERT_EQ(ZX_POL_ACTION_KILL, p.QueryBasicPolicy(ZX_POL_NEW_CHANNEL), "");

    const JobPolicy orig = p;

    zx_policy_basic_t new_policy{ZX_POL_NEW_ANY, UINT32_MAX};
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, p.AddBasicPolicy(ZX_JOB_POL_ABSOLUTE, &new_policy, 1), "");
    ASSERT_TRUE(orig == p, "");

    new_policy = {ZX_POL_NEW_VMO, ZX_POL_ACTION_ALLOW};
    ASSERT_EQ(ZX_ERR_ALREADY_EXISTS, p.AddBasicPolicy(ZX_JOB_POL_ABSOLUTE, &new_policy, 1), "");
    ASSERT_TRUE(orig == p, "");

    END_TEST;
}

static bool add_basic_policy_deny_any() {
    BEGIN_TEST;

    JobPolicy p;
    zx_policy_basic_t policy{ZX_POL_NEW_ANY, ZX_POL_ACTION_DENY};
    ASSERT_EQ(ZX_OK, p.AddBasicPolicy(ZX_JOB_POL_ABSOLUTE, &policy, 1), "");
    ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_VMO), "");
    ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_CHANNEL), "");
    ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_EVENT), "");
    ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_EVENTPAIR), "");
    ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_PORT), "");
    ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_SOCKET), "");
    ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_FIFO), "");
    ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_TIMER), "");
    ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_PROCESS), "");
    ASSERT_EQ(ZX_POL_ACTION_DENY, p.QueryBasicPolicy(ZX_POL_NEW_PROFILE), "");

    END_TEST;
}

static bool set_get_timer_slack() {
    BEGIN_TEST;

    JobPolicy p;
    p.SetTimerSlack({1200, TIMER_SLACK_EARLY});
    ASSERT_EQ(1200, p.GetTimerSlack().amount(), "");
    ASSERT_EQ(TIMER_SLACK_EARLY, p.GetTimerSlack().mode(), "");

    END_TEST;
}

} // namespace

UNITTEST_START_TESTCASE(job_policy_tests)
UNITTEST("initial_state", initial_state)
UNITTEST("add_basic_policy_no_widening", add_basic_policy_no_widening)
UNITTEST("add_basic_policy_no_widening_with_any", add_basic_policy_no_widening_with_any)
UNITTEST("add_basic_policy_absolute", add_basic_policy_absolute)
UNITTEST("add_basic_policy_relative", add_basic_policy_relative)
UNITTEST("add_basic_policy_unmodified_on_error", add_basic_policy_unmodified_on_error)
UNITTEST("add_basic_policy_deny_any", add_basic_policy_deny_any)
UNITTEST("set_get_timer_slack", set_get_timer_slack)
UNITTEST_END_TESTCASE(job_policy_tests, "job_policy", "JobPolicy tests");
