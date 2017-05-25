// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/policy.h>

#include <mini-process/mini-process.h>
#include <unittest/unittest.h>

static const char process_name[] = "job-test-p";

extern mx_handle_t root_job;

static bool basic_test(void) {
    BEGIN_TEST;

    // Never close the launchpad job.
    mx_handle_t job_parent = mx_job_default();
    ASSERT_NEQ(job_parent, MX_HANDLE_INVALID, "");

    // If the parent job is valid, one should be able to create a child job
    // and a child job of the child job.
    mx_handle_t job_child, job_grandchild;
    ASSERT_EQ(mx_job_create(job_parent, 0u, &job_child), NO_ERROR, "");
    ASSERT_EQ(mx_job_create(job_child, 0u, &job_grandchild), NO_ERROR, "");

    ASSERT_EQ(mx_handle_close(job_child), NO_ERROR, "");
    ASSERT_EQ(mx_handle_close(job_grandchild), NO_ERROR, "");

    // If the parent job is not valid it should fail.
    mx_handle_t job_fail;
    ASSERT_EQ(mx_job_create(MX_HANDLE_INVALID, 0u, &job_fail), ERR_BAD_HANDLE, "");

    END_TEST;
}

static bool create_test(void) {
    BEGIN_TEST;

    mx_handle_t job_parent = mx_job_default();
    ASSERT_NEQ(job_parent, MX_HANDLE_INVALID, "");

    mx_handle_t job_child;
    ASSERT_EQ(mx_job_create(job_parent, 0u, &job_child), NO_ERROR, "");

    // Make sure we can create process object with both the parent job and a child job.
    mx_handle_t process1, vmar1;
    ASSERT_EQ(mx_process_create(
        job_parent, process_name, sizeof(process_name), 0u, &process1, &vmar1), NO_ERROR, "");

    mx_handle_t process2, vmar2;
    ASSERT_EQ(mx_process_create(
        job_child, process_name, sizeof(process_name), 0u, &process2, &vmar2), NO_ERROR, "");

    ASSERT_EQ(mx_handle_close(job_child), NO_ERROR, "");
    ASSERT_EQ(mx_handle_close(process1), NO_ERROR, "");
    ASSERT_EQ(mx_handle_close(process2), NO_ERROR, "");
    ASSERT_EQ(mx_handle_close(vmar1), NO_ERROR, "");
    ASSERT_EQ(mx_handle_close(vmar2), NO_ERROR, "");

    END_TEST;
}

static bool policy_basic_test(void) {
    BEGIN_TEST;

    mx_handle_t job_parent = mx_job_default();
    ASSERT_NEQ(job_parent, MX_HANDLE_INVALID, "");

    mx_handle_t job_child;
    ASSERT_EQ(mx_job_create(job_parent, 0u, &job_child), NO_ERROR, "");

    mx_policy_basic_t policy[] = {
        { MX_POL_BAD_HANDLE, MX_POL_ACTION_KILL },
        { MX_POL_NEW_CHANNEL, MX_POL_ACTION_ALLOW | MX_POL_ACTION_ALARM },
        { MX_POL_NEW_FIFO, MX_POL_ACTION_DENY },
    };

    ASSERT_EQ(mx_job_set_policy(job_child, MX_JOB_POL_RELATIVE,
        MX_JOB_POL_BASIC, policy, countof(policy)), NO_ERROR, "");

    ASSERT_EQ(mx_handle_close(job_child), NO_ERROR, "");
    END_TEST;
}

static bool kill_test(void) {
    BEGIN_TEST;

    mx_handle_t job_parent = mx_job_default();
    ASSERT_NEQ(job_parent, MX_HANDLE_INVALID, "");

    mx_handle_t job_child;
    ASSERT_EQ(mx_job_create(job_parent, 0u, &job_child), NO_ERROR, "");

    mx_handle_t event;
    ASSERT_EQ(mx_event_create(0u, &event), NO_ERROR, "");

    mx_handle_t process, thread;
    ASSERT_EQ(start_mini_process(job_child, event, &process, &thread), NO_ERROR, "");

    ASSERT_EQ(mx_task_kill(job_child), NO_ERROR, "");

    mx_signals_t signals;
    ASSERT_EQ(mx_object_wait_one(
        process, MX_TASK_TERMINATED, MX_TIME_INFINITE, &signals), NO_ERROR, "");
    ASSERT_EQ(signals, MX_TASK_TERMINATED | MX_SIGNAL_LAST_HANDLE, "");

    ASSERT_EQ(mx_object_wait_one(
        job_child, MX_JOB_NO_PROCESSES, MX_TIME_INFINITE, &signals), NO_ERROR, "");
    ASSERT_EQ(signals, MX_JOB_NO_PROCESSES | MX_JOB_NO_JOBS | MX_SIGNAL_LAST_HANDLE, "");

    END_TEST;
}

static bool wait_test(void) {
    BEGIN_TEST;

    mx_handle_t job_parent = mx_job_default();
    ASSERT_NEQ(job_parent, MX_HANDLE_INVALID, "");

    mx_handle_t job_child;
    ASSERT_EQ(mx_job_create(job_parent, 0u, &job_child), NO_ERROR, "");

    mx_handle_t event;
    ASSERT_EQ(mx_event_create(0u, &event), NO_ERROR, "");

    mx_handle_t process, thread;
    ASSERT_EQ(start_mini_process(job_child, event, &process, &thread), NO_ERROR, "");

    mx_signals_t signals;
    ASSERT_EQ(mx_object_wait_one(
        job_child, MX_JOB_NO_JOBS, MX_TIME_INFINITE, &signals), NO_ERROR, "");
    ASSERT_EQ(signals, MX_JOB_NO_JOBS | MX_SIGNAL_LAST_HANDLE, "");

    mx_nanosleep(mx_deadline_after(MX_MSEC(5)));
    ASSERT_EQ(mx_task_kill(process), NO_ERROR, "");

    ASSERT_EQ(mx_object_wait_one(
        job_child, MX_JOB_NO_PROCESSES, MX_TIME_INFINITE, &signals), NO_ERROR, "");
    ASSERT_EQ(signals, MX_JOB_NO_PROCESSES | MX_JOB_NO_JOBS | MX_SIGNAL_LAST_HANDLE, "");

    ASSERT_EQ(mx_handle_close(thread), NO_ERROR, "");
    ASSERT_EQ(mx_handle_close(process), NO_ERROR, "");
    ASSERT_EQ(mx_handle_close(job_child), NO_ERROR, "");

    END_TEST;
}

static bool info_task_stats_fails(void) {
    BEGIN_TEST;
    mx_info_task_stats_t info;
    ASSERT_NEQ(mx_object_get_info(mx_job_default(), MX_INFO_TASK_STATS,
                                  &info, sizeof(info), NULL, NULL),
               NO_ERROR,
               "Just added job support to info_task_status?");
    // If so, replace this with a real test; see example in process.cpp.
    END_TEST;
}

// Returns the job's MX_PROP_JOB_MAX_HEIGHT property value.
static uint32_t get_job_max_height(mx_handle_t job) {
    uint32_t value;
    mx_status_t s = mx_object_get_property(
        job, MX_PROP_JOB_MAX_HEIGHT, &value, sizeof(value));
    if (s != NO_ERROR) {
        // Poison the test and return an unlikely value.
        EXPECT_EQ(NO_ERROR, s, "");
        return 0xffffffffu;
    }
    return value;
}

// Show that max height decreases by generation, and that jobs with
// a max height of zero cannot have child jobs.
static bool max_height_smoke(void) {
    BEGIN_TEST;

    // Get our parent job and its max height value.
    mx_handle_t parent_job = mx_job_default();
    uint32_t parent_job_mh = get_job_max_height(parent_job);
    // Make sure it's a not-too-big positive value
    ASSERT_GT(parent_job_mh, 0u, "");
    ASSERT_LT(parent_job_mh, 64u, "");

    // Stack of handles that we need to close.
    mx_handle_t *handles = calloc(parent_job_mh, sizeof(*handles));
    ASSERT_NONNULL(handles, "");
    mx_handle_t *htop = handles;

    // Eat up our max height, demonstrating that the value decreases for
    // each generation.
    while (parent_job_mh > 0) {
        mx_handle_t child_job;
        ASSERT_EQ(mx_job_create(parent_job, 0u, &child_job), NO_ERROR, "");
        uint32_t child_job_mh = get_job_max_height(child_job);
        // ASSERT rather than EXPECT so we don't sit in this loop forever.
        ASSERT_EQ(parent_job_mh - 1, child_job_mh, "");

        *htop++ = child_job;
        parent_job = child_job;
        parent_job_mh = child_job_mh;
    }

    // We've hit the bottom. Creating a child under this job should fail.
    mx_handle_t child_job;
    EXPECT_EQ(mx_job_create(parent_job, 0u, &child_job), ERR_OUT_OF_RANGE, "");

    // Creating a process should succeed, though.
    mx_handle_t child_proc;
    mx_handle_t vmar;
    ASSERT_EQ(mx_process_create(
                  parent_job, "test", sizeof("test"), 0u, &child_proc, &vmar),
              NO_ERROR, "");
    mx_handle_close(vmar);
    mx_handle_close(child_proc);

    // Clean up.
    while (htop > handles) {
        EXPECT_EQ(mx_handle_close(*--htop), NO_ERROR, "");
    }
    free(handles);

    END_TEST;
}

static bool set_max_height_fails(void) {
    BEGIN_TEST;

    mx_handle_t job;
    ASSERT_EQ(mx_job_create(mx_job_default(), 0u, &job), NO_ERROR, "");
    uint32_t mh = get_job_max_height(job);

    // Setting the max height should fail.
    uint32_t new_mh = mh - 1;
    EXPECT_NEQ(mx_object_set_property(
        job, MX_PROP_JOB_MAX_HEIGHT, &new_mh, sizeof(new_mh)), NO_ERROR, "");

    // The max height value should not have changed.
    EXPECT_EQ(mh, get_job_max_height(job), "");

    mx_handle_close(job);

    END_TEST;
}

BEGIN_TEST_CASE(job_tests)
RUN_TEST(basic_test)
RUN_TEST(policy_basic_test)
RUN_TEST(create_test)
RUN_TEST(kill_test)
RUN_TEST(wait_test)
RUN_TEST(info_task_stats_fails)
RUN_TEST(max_height_smoke)
RUN_TEST(set_max_height_fails)
END_TEST_CASE(job_tests)
