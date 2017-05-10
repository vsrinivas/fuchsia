// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdio.h>
#include <threads.h>

#include <mx/channel.h>
#include <mx/event.h>
#include <mx/handle.h>
#include <mx/job.h>
#include <mx/port.h>
#include <mx/process.h>
#include <mx/thread.h>
#include <mx/vmar.h>

#include <mxtl/type_support.h>

#include <magenta/process.h>
#include <magenta/syscalls/policy.h>
#include <magenta/syscalls/port.h>

#include <mini-process/mini-process.h>

#include <unistd.h>
#include <unittest/unittest.h>

// Basic job operation is tested by core-tests.
static mx::job make_job() {
    mx::job job;
    if (mx::job::create(mx_job_default(), 0u, &job) != NO_ERROR)
        return mx::job();
    return job;
}

static mx::process make_test_process(const mx::job& job, mx_handle_t* ctrl) {
    mx::vmar vmar;
    mx::process proc;
    mx_status_t status = mx::process::create(job, "poltst", 6u, 0u, &proc, &vmar);
    if (status != NO_ERROR)
        return mx::process();

    mx::thread thread;
    status = mx::thread::create(proc, "poltst", 6u, 0, &thread);
    if (status != NO_ERROR)
        return mx::process();

    mx::event event;
    status = mx::event::create(0u, &event);
    if (status != NO_ERROR)
        return mx::process();

    auto thr = thread.release();
    status = start_mini_process_etc(proc.get(), thr, vmar.get(), event.release(), ctrl);
    if (status != NO_ERROR)
        return mx::process();

    return proc;
}

static bool abs_then_rel() {
    BEGIN_TEST;

    mx_policy_basic_t policy[] = {
        { MX_POL_BAD_HANDLE, MX_POL_ACTION_KILL } };

    auto job = make_job();
    EXPECT_EQ(job.set_policy(
        MX_JOB_POL_ABSOLUTE, MX_JOB_POL_BASIC, policy, countof(policy)), NO_ERROR, "");

    // A contradictory policy should fail.
    policy[0].policy = MX_POL_ACTION_ALARM | MX_POL_ACTION_DENY;
    EXPECT_EQ(job.set_policy(
        MX_JOB_POL_ABSOLUTE, MX_JOB_POL_BASIC, policy, countof(policy)), ERR_ALREADY_EXISTS, "");

    // The same again will succeed.
    policy[0].policy = MX_POL_ACTION_KILL;
    EXPECT_EQ(job.set_policy(
        MX_JOB_POL_ABSOLUTE, MX_JOB_POL_BASIC, policy, countof(policy)), NO_ERROR, "");

    // A contradictory relative policy will succeed, but is a no-op
    policy[0].policy = MX_POL_ACTION_ALLOW;
    EXPECT_EQ(job.set_policy(
        MX_JOB_POL_RELATIVE, MX_JOB_POL_BASIC, policy, countof(policy)), NO_ERROR, "");

    mx_policy_basic_t more[] = {
        { MX_POL_NEW_CHANNEL, MX_POL_ACTION_ALLOW | MX_POL_ACTION_ALARM },
        { MX_POL_NEW_FIFO, MX_POL_ACTION_DENY } };

    // Addtional absolute policy that don't contractict existing can be added.
    EXPECT_EQ(job.set_policy(
        MX_JOB_POL_ABSOLUTE, MX_JOB_POL_BASIC, more, countof(more)), NO_ERROR, "");

    END_TEST;
}

static bool invalid_calls(uint32_t options) {
    BEGIN_TEST;

    auto job = make_job();

    EXPECT_EQ(job.set_policy(options, MX_JOB_POL_BASIC, nullptr, 0u), ERR_INVALID_ARGS, "");

    EXPECT_EQ(job.set_policy(options, MX_JOB_POL_BASIC, nullptr, 5u), ERR_INVALID_ARGS, "");

    mx_policy_basic_t policy1[] = {
        { MX_POL_BAD_HANDLE, MX_POL_ACTION_KILL },
    };

    EXPECT_EQ(job.set_policy(options, MX_JOB_POL_BASIC, policy1, 0u), ERR_INVALID_ARGS, "");

    mx_policy_basic_t policy2[] = {
        { 100001u, MX_POL_ACTION_KILL },
    };

    EXPECT_EQ(job.set_policy(
        options, MX_JOB_POL_BASIC, policy2, countof(policy2)), ERR_INVALID_ARGS, "");

    mx_policy_basic_t policy3[] = {
        { MX_POL_BAD_HANDLE, 100001u },
    };

    EXPECT_EQ(job.set_policy(
        options, MX_JOB_POL_BASIC, policy3, countof(policy2)), ERR_NOT_SUPPORTED, "");

    // The job still will acept a valid combination:
    mx_policy_basic_t policy4[] = {
        { MX_POL_BAD_HANDLE, MX_POL_ACTION_KILL } };

    EXPECT_EQ(job.set_policy(
        options, MX_JOB_POL_BASIC, policy4, countof(policy4)), NO_ERROR, "");

    END_TEST;
}

static bool invalid_calls_abs() {
    return invalid_calls(MX_JOB_POL_ABSOLUTE);
}

static bool invalid_calls_rel() {
    return invalid_calls(MX_JOB_POL_RELATIVE);
}

static bool enforce_creation_pol(
    mx_policy_basic_t* pol, uint32_t pol_count, uint32_t minip_cmd, mx_status_t expect) {
    BEGIN_TEST;
    auto job = make_job();

    ASSERT_EQ(job.set_policy(
        MX_JOB_POL_ABSOLUTE, MX_JOB_POL_BASIC, pol, pol_count), NO_ERROR, "");

    mx_handle_t ctrl;
    auto proc = make_test_process(job, &ctrl);
    ASSERT_TRUE(proc.is_valid(), "");
    ASSERT_NEQ(ctrl, MX_HANDLE_INVALID, "");

    mx_handle_t obj;
    EXPECT_EQ(mini_process_cmd(ctrl, minip_cmd, &obj), expect, "");
    EXPECT_EQ(mini_process_cmd(ctrl, MINIP_CMD_EXIT_NORMAL, nullptr), ERR_PEER_CLOSED, "");

    mx_handle_close(ctrl);
    END_TEST;
}

static bool enforce_deny_event() {
    mx_policy_basic_t policy[] = { { MX_POL_NEW_EVENT, MX_POL_ACTION_DENY } };
    return enforce_creation_pol(
        policy, countof(policy), MINIP_CMD_CREATE_EVENT, ERR_ACCESS_DENIED);
}

static bool enforce_deny_channel() {
    mx_policy_basic_t policy[] = { { MX_POL_NEW_CHANNEL, MX_POL_ACTION_DENY } };
    return enforce_creation_pol(
        policy, countof(policy), MINIP_CMD_CREATE_CHANNEL, ERR_ACCESS_DENIED);
}

static bool enforce_deny_any() {
    mx_policy_basic_t policy[] = { { MX_POL_NEW_ANY, MX_POL_ACTION_DENY } };
    return enforce_creation_pol(
        policy, countof(policy), MINIP_CMD_CREATE_EVENT, ERR_ACCESS_DENIED);
}

static bool enforce_allow_any() {
    mx_policy_basic_t policy[] = { { MX_POL_NEW_ANY, MX_POL_ACTION_ALLOW } };
    return enforce_creation_pol(
        policy, countof(policy), MINIP_CMD_CREATE_EVENT, NO_ERROR);
}

static bool enforce_deny_but_event() {
    mx_policy_basic_t policy[] = {
        { MX_POL_NEW_ANY, MX_POL_ACTION_DENY },
        { MX_POL_NEW_EVENT, MX_POL_ACTION_ALLOW }
    };
    auto res = enforce_creation_pol(
        policy, countof(policy), MINIP_CMD_CREATE_EVENT, NO_ERROR);
    return res && enforce_creation_pol(
        policy, countof(policy), MINIP_CMD_CREATE_CHANNEL, ERR_ACCESS_DENIED);
}

BEGIN_TEST_CASE(job_policy)
RUN_TEST(invalid_calls_abs)
RUN_TEST(invalid_calls_rel)
RUN_TEST(abs_then_rel)
RUN_TEST(enforce_deny_event)
RUN_TEST(enforce_deny_channel)
RUN_TEST(enforce_deny_any)
RUN_TEST(enforce_allow_any)
RUN_TEST(enforce_deny_but_event)
END_TEST_CASE(job_policy)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
