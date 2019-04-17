// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <zircon/process.h>
#include <zircon/rights.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/policy.h>

#include <fbl/algorithm.h>
#include <lib/zx/job.h>
#include <mini-process/mini-process.h>
#include <unittest/unittest.h>

static const char process_name[] = "job-test-p";

extern zx_handle_t root_job;

static bool basic_test() {
    BEGIN_TEST;

    // Never close the launchpad job.
    zx_handle_t job_parent = zx_job_default();
    ASSERT_NE(job_parent, ZX_HANDLE_INVALID, "");

    // If the parent job is valid, one should be able to create a child job
    // and a child job of the child job.
    zx_handle_t job_child, job_grandchild;
    ASSERT_EQ(zx_job_create(job_parent, 0u, &job_child), ZX_OK, "");
    ASSERT_EQ(zx_job_create(job_child, 0u, &job_grandchild), ZX_OK, "");

    zx_info_job_t job_info;
    ASSERT_EQ(zx_object_get_info(
            job_child, ZX_INFO_JOB, &job_info, sizeof(job_info), NULL, NULL), ZX_OK);
    EXPECT_FALSE(job_info.exited);
    EXPECT_EQ(job_info.return_code, 0, "");

    ASSERT_EQ(zx_handle_close(job_child), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(job_grandchild), ZX_OK, "");

    // If the parent job is not valid it should fail.
    zx_handle_t job_fail;
    ASSERT_EQ(zx_job_create(ZX_HANDLE_INVALID, 0u, &job_fail), ZX_ERR_BAD_HANDLE, "");

    END_TEST;
}

static bool create_test() {
    BEGIN_TEST;

    zx_handle_t job_parent = zx_job_default();
    ASSERT_NE(job_parent, ZX_HANDLE_INVALID, "");

    zx_handle_t job_child;
    ASSERT_EQ(zx_job_create(job_parent, 0u, &job_child), ZX_OK, "");

    // Make sure we can create process object with both the parent job and a child job.
    zx_handle_t process1, vmar1;
    ASSERT_EQ(zx_process_create(
        job_parent, process_name, sizeof(process_name), 0u, &process1, &vmar1), ZX_OK, "");

    zx_handle_t process2, vmar2;
    ASSERT_EQ(zx_process_create(
        job_child, process_name, sizeof(process_name), 0u, &process2, &vmar2), ZX_OK, "");

    ASSERT_EQ(zx_handle_close(job_child), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(process1), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(process2), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(vmar1), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(vmar2), ZX_OK, "");

    END_TEST;
}

static bool create_missing_rights_test() {
    BEGIN_TEST;

    zx_rights_t rights = ZX_DEFAULT_JOB_RIGHTS & ~ZX_RIGHT_WRITE & ~ZX_RIGHT_MANAGE_JOB;
    zx_handle_t job_parent;
    zx_status_t status = zx_handle_duplicate(zx_job_default(), rights, &job_parent);
    ASSERT_EQ(status, ZX_OK, "");

    zx_handle_t job_child;
    ASSERT_EQ(zx_job_create(job_parent, 0u, &job_child), ZX_ERR_ACCESS_DENIED, "");

    zx_handle_close(job_parent);

    END_TEST;
}

static bool policy_invalid_topic_test() {
    BEGIN_TEST;

    zx::job job_child;
    ASSERT_EQ(zx::job::create(*zx::job::default_job(), 0u, &job_child), ZX_OK, "");

    const uint32_t invalid_topic = 2u;
    const uint32_t some_policy = 0;
    ASSERT_EQ(job_child.set_policy(ZX_JOB_POL_RELATIVE, invalid_topic, &some_policy, 1),
              ZX_ERR_INVALID_ARGS, "");

    END_TEST;
}

static bool policy_basic_test() {
    BEGIN_TEST;

    zx::job job_child;
    ASSERT_EQ(zx::job::create(*zx::job::default_job(), 0u, &job_child), ZX_OK, "");

    zx_policy_basic_t policy[] = {
        { ZX_POL_BAD_HANDLE, ZX_POL_ACTION_KILL },
        { ZX_POL_NEW_CHANNEL, ZX_POL_ACTION_ALLOW | ZX_POL_ACTION_EXCEPTION },
        { ZX_POL_NEW_FIFO, ZX_POL_ACTION_DENY },
    };

    ASSERT_EQ(job_child.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_BASIC, policy, fbl::count_of(policy)),
              ZX_OK, "");

    END_TEST;
}

static bool policy_timer_slack_invalid_options_test() {
    BEGIN_TEST;

    zx::job job_child;
    ASSERT_EQ(zx::job::create(*zx::job::default_job(), 0u, &job_child), ZX_OK, "");

    zx_policy_timer_slack policy = {ZX_MSEC(10), ZX_TIMER_SLACK_LATE};

    // Invalid.
    uint32_t options = ZX_JOB_POL_ABSOLUTE;
    ASSERT_EQ(job_child.set_policy(options, ZX_JOB_POL_TIMER_SLACK, &policy, 1),
              ZX_ERR_INVALID_ARGS, "");

    // Valid.
    options = ZX_JOB_POL_RELATIVE;
    ASSERT_EQ(job_child.set_policy(options, ZX_JOB_POL_TIMER_SLACK, &policy, 1),
              ZX_OK, "");

    END_TEST;
}

static bool policy_timer_slack_invalid_count_test() {
    BEGIN_TEST;

    zx::job job_child;
    ASSERT_EQ(zx::job::create(*zx::job::default_job(), 0u, &job_child), ZX_OK, "");

    zx_policy_timer_slack policy[2] = {{ZX_MSEC(10), ZX_TIMER_SLACK_LATE},
                                       {ZX_MSEC(10), ZX_TIMER_SLACK_LATE}};

    // Too few.
    ASSERT_EQ(job_child.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_TIMER_SLACK, &policy, 0),
              ZX_ERR_INVALID_ARGS, "");

    // Too many.
    ASSERT_EQ(job_child.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_TIMER_SLACK, &policy, 2),
              ZX_ERR_INVALID_ARGS, "");

    // Just right.
    ASSERT_EQ(job_child.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_TIMER_SLACK, &policy, 1), ZX_OK,
              "");

    END_TEST;
}

static bool policy_timer_slack_invalid_policy_test() {
    BEGIN_TEST;

    zx::job job_child;
    ASSERT_EQ(zx::job::create(*zx::job::default_job(), 0u, &job_child), ZX_OK, "");

    // Null.
    ASSERT_EQ(job_child.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_TIMER_SLACK, nullptr, 1),
              ZX_ERR_INVALID_ARGS, "");

    // Negative amount.
    zx_policy_timer_slack policy = {-ZX_MSEC(10), ZX_TIMER_SLACK_LATE};
    ASSERT_EQ(job_child.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_TIMER_SLACK, &policy, 1),
              ZX_ERR_INVALID_ARGS, "");

    // Invalid mode.
    policy = {ZX_MSEC(10), 3};
    ASSERT_EQ(job_child.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_TIMER_SLACK, &policy, 1),
              ZX_ERR_INVALID_ARGS, "");

    // OK.
    policy = {ZX_MSEC(10), ZX_TIMER_SLACK_LATE};
    ASSERT_EQ(job_child.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_TIMER_SLACK, &policy, 1), ZX_OK,
              "");

    END_TEST;
}

static bool policy_timer_slack_non_empty_test() {
    BEGIN_TEST;

    zx::job job_child;
    ASSERT_EQ(zx::job::create(*zx::job::default_job(), 0u, &job_child), ZX_OK, "");
    zx::job job_grandchild;
    ASSERT_EQ(zx::job::create(job_child, 0u, &job_grandchild), ZX_OK, "");

    zx_policy_timer_slack policy = {ZX_MSEC(10), ZX_TIMER_SLACK_LATE};

    // The job isn't empty.
    ASSERT_EQ(job_child.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_TIMER_SLACK, &policy, 1),
              ZX_ERR_BAD_STATE, "");

    job_grandchild.reset();

    // Job is now empty.
    ASSERT_EQ(job_child.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_TIMER_SLACK, &policy, 1), ZX_OK,
              "");

    END_TEST;
}

static bool policy_timer_slack_valid() {
    BEGIN_TEST;

    zx::job job_child;
    ASSERT_EQ(zx::job::create(*zx::job::default_job(), 0u, &job_child), ZX_OK, "");

    // All modes.
    zx_policy_timer_slack policy = {ZX_MSEC(10), ZX_TIMER_SLACK_CENTER};
    ASSERT_EQ(job_child.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_TIMER_SLACK, &policy, 1), ZX_OK,
              "");
    policy = {ZX_MSEC(10), ZX_TIMER_SLACK_EARLY};
    ASSERT_EQ(job_child.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_TIMER_SLACK, &policy, 1), ZX_OK,
              "");
    policy = {ZX_MSEC(10), ZX_TIMER_SLACK_LATE};
    ASSERT_EQ(job_child.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_TIMER_SLACK, &policy, 1), ZX_OK,
              "");

    // Raise the minimium.
    policy = {ZX_SEC(10), ZX_TIMER_SLACK_LATE};
    ASSERT_EQ(job_child.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_TIMER_SLACK, &policy, 1), ZX_OK,
              "");

    // Try to lower the minimium, no error.
    policy = {ZX_USEC(5), ZX_TIMER_SLACK_CENTER};
    ASSERT_EQ(job_child.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_TIMER_SLACK, &policy, 1), ZX_OK,
              "");

    END_TEST;
}

static bool kill_test() {
    BEGIN_TEST;

    zx_handle_t job_parent = zx_job_default();
    ASSERT_NE(job_parent, ZX_HANDLE_INVALID, "");

    zx_handle_t job_child;
    ASSERT_EQ(zx_job_create(job_parent, 0u, &job_child), ZX_OK, "");

    zx_handle_t event;
    ASSERT_EQ(zx_event_create(0u, &event), ZX_OK, "");

    zx_handle_t process, thread;
    ASSERT_EQ(start_mini_process(job_child, event, &process, &thread), ZX_OK, "");

    ASSERT_EQ(zx_task_kill(job_child), ZX_OK, "");

    zx_signals_t signals;
    ASSERT_EQ(zx_object_wait_one(
        process, ZX_TASK_TERMINATED, ZX_TIME_INFINITE, &signals), ZX_OK, "");
    ASSERT_EQ(signals, ZX_TASK_TERMINATED, "");

    ASSERT_EQ(zx_object_wait_one(
        job_child, ZX_JOB_NO_PROCESSES, ZX_TIME_INFINITE, &signals), ZX_OK, "");
    ASSERT_EQ(signals, __ZX_JOB_NO_PROCESSES_OLD | ZX_JOB_NO_PROCESSES | ZX_JOB_NO_JOBS, "");

    // Process should be in the dead state here.
    zx_info_job_t job_info;
    ASSERT_EQ(zx_object_get_info(
            job_child, ZX_INFO_JOB, &job_info, sizeof(job_info), NULL, NULL), ZX_OK);
    EXPECT_TRUE(job_info.exited);
    EXPECT_EQ(job_info.return_code, ZX_TASK_RETCODE_SYSCALL_KILL, "");

    zx_info_process_t proc_info;
    ASSERT_EQ(zx_object_get_info(
            process, ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), NULL, NULL), ZX_OK);
    EXPECT_TRUE(proc_info.exited);
    EXPECT_EQ(proc_info.return_code, ZX_TASK_RETCODE_SYSCALL_KILL, "");

    // Can't create more processes or jobs.

    zx_handle_t job_grandchild;
    ASSERT_EQ(zx_job_create(job_child, 0u, &job_grandchild), ZX_ERR_BAD_STATE, "");

    ASSERT_EQ(zx_handle_close(thread), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(process), ZX_OK, "");
    ASSERT_EQ(start_mini_process(job_child, event, &process, &thread), ZX_ERR_BAD_STATE, "");

    ASSERT_EQ(zx_handle_close(job_child), ZX_OK, "");
    END_TEST;
}

static bool kill_job_no_child_test() {
    BEGIN_TEST;

    zx_handle_t job_parent = zx_job_default();
    ASSERT_NE(job_parent, ZX_HANDLE_INVALID, "");

    zx_handle_t job_child;
    ASSERT_EQ(zx_job_create(job_parent, 0u, &job_child), ZX_OK, "");

    ASSERT_EQ(zx_task_kill(job_child), ZX_OK, "");

    zx_handle_t job_grandchild;
    ASSERT_EQ(zx_job_create(job_child, 0u, &job_grandchild), ZX_ERR_BAD_STATE, "");

    zx_handle_t event;
    ASSERT_EQ(zx_event_create(0u, &event), ZX_OK, "");

    zx_handle_t process, thread;
    ASSERT_EQ(start_mini_process(job_child, event, &process, &thread), ZX_ERR_BAD_STATE, "");

    ASSERT_EQ(zx_handle_close(job_child), ZX_OK, "");

    END_TEST;
}

static bool set_job_oom_kill_bit() {
    BEGIN_TEST;
    // TODO(cpu): Other than trivial set/reset of the property this can't be
    // fully tested without de-establizing the system under test. The current
    // best way to test this is to boot the full stack and issue in a console
    //   $k oom lowmem
    // And watch the kernel log output.

    size_t oom = 1u;
    ASSERT_EQ(zx_object_set_property(
        zx_job_default(), ZX_PROP_JOB_KILL_ON_OOM, &oom, sizeof(size_t)), ZX_OK, "");

    oom = 0u;
    ASSERT_EQ(zx_object_set_property(
        zx_job_default(), ZX_PROP_JOB_KILL_ON_OOM, &oom, sizeof(size_t)), ZX_OK, "");

    oom = 2u;
    ASSERT_EQ(zx_object_set_property(
        zx_job_default(), ZX_PROP_JOB_KILL_ON_OOM, &oom, sizeof(size_t)), ZX_ERR_INVALID_ARGS, "");

    END_TEST;
}

static bool wait_test() {
    BEGIN_TEST;

    zx_handle_t job_parent = zx_job_default();
    ASSERT_NE(job_parent, ZX_HANDLE_INVALID, "");

    zx_handle_t job_child;
    ASSERT_EQ(zx_job_create(job_parent, 0u, &job_child), ZX_OK, "");

    zx_handle_t event;
    ASSERT_EQ(zx_event_create(0u, &event), ZX_OK, "");

    zx_handle_t process, thread;
    ASSERT_EQ(start_mini_process(job_child, event, &process, &thread), ZX_OK, "");

    zx_signals_t signals;
    ASSERT_EQ(zx_object_wait_one(
        job_child, ZX_JOB_NO_JOBS, ZX_TIME_INFINITE, &signals), ZX_OK, "");
    ASSERT_EQ(signals, ZX_JOB_NO_JOBS, "");

    zx_nanosleep(zx_deadline_after(ZX_MSEC(5)));
    ASSERT_EQ(zx_task_kill(process), ZX_OK, "");

    ASSERT_EQ(zx_object_wait_one(
        job_child, ZX_JOB_NO_PROCESSES, ZX_TIME_INFINITE, &signals), ZX_OK, "");
    ASSERT_EQ(signals, __ZX_JOB_NO_PROCESSES_OLD | ZX_JOB_NO_PROCESSES | ZX_JOB_NO_JOBS, "");

    ASSERT_EQ(zx_handle_close(thread), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(process), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(job_child), ZX_OK, "");

    END_TEST;
}

static bool info_task_stats_fails() {
    BEGIN_TEST;
    zx_info_task_stats_t info;
    ASSERT_NE(zx_object_get_info(zx_job_default(), ZX_INFO_TASK_STATS,
                                 &info, sizeof(info), NULL, NULL),
              ZX_OK,
              "Just added job support to info_task_status?");
    // If so, replace this with a real test; see example in process.cpp.
    END_TEST;
}

// Show that there is a max job height.
static bool max_height_smoke() {
    BEGIN_TEST;

    // Get our parent job.
    zx_handle_t parent_job = zx_job_default();

    // Stack of handles that we need to close.
    static const int kNumJobs = 128;
    zx_handle_t* handles = static_cast<zx_handle_t*>(calloc(kNumJobs, sizeof(*handles)));
    ASSERT_NONNULL(handles, "");
    zx_handle_t* htop = handles;

    // Eat up our max height.
    while (true) {
        zx_handle_t child_job;
        zx_status_t s = zx_job_create(parent_job, 0u, &child_job);
        if (s != ZX_OK) {
            break;
        }
        // We should hit the max before running out of entries;
        // this is the core check of this test.
        ASSERT_LT(htop - handles, kNumJobs,
                  "Should have seen the max job height");
        *htop++ = child_job;
        parent_job = child_job;
    }

    // We've hit the bottom. Creating a child under this job should fail.
    zx_handle_t child_job;
    EXPECT_EQ(zx_job_create(parent_job, 0u, &child_job), ZX_ERR_OUT_OF_RANGE, "");

    // Creating a process should succeed, though.
    zx_handle_t child_proc;
    zx_handle_t vmar;
    ASSERT_EQ(zx_process_create(
                  parent_job, "test", sizeof("test"), 0u, &child_proc, &vmar),
              ZX_OK, "");
    zx_handle_close(vmar);
    zx_handle_close(child_proc);

    // Clean up.
    while (htop > handles) {
        EXPECT_EQ(zx_handle_close(*--htop), ZX_OK, "");
    }
    free(handles);

    END_TEST;
}

BEGIN_TEST_CASE(job_tests)
RUN_TEST(basic_test)
RUN_TEST(create_missing_rights_test)
RUN_TEST(policy_invalid_topic_test)
RUN_TEST(policy_basic_test)
RUN_TEST(policy_timer_slack_invalid_options_test)
RUN_TEST(policy_timer_slack_invalid_count_test)
RUN_TEST(policy_timer_slack_invalid_policy_test)
RUN_TEST(policy_timer_slack_non_empty_test)
RUN_TEST(policy_timer_slack_valid)
// For verifying timer slack correctness, see |timer_diag()| in kernel/tests/timer_tests.cpp or run
// "k timer_diag".
RUN_TEST(create_test)
RUN_TEST(kill_test)
RUN_TEST(kill_job_no_child_test)
RUN_TEST(set_job_oom_kill_bit)
RUN_TEST(wait_test)
RUN_TEST(info_task_stats_fails)
RUN_TEST(max_height_smoke)
END_TEST_CASE(job_tests)
