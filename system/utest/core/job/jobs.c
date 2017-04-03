// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <magenta/process.h>
#include <magenta/syscalls.h>
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
    ASSERT_EQ(signals, MX_TASK_TERMINATED, "");

    ASSERT_EQ(mx_object_wait_one(
        job_child, MX_JOB_NO_PROCESSES, MX_TIME_INFINITE, &signals), NO_ERROR, "");
    ASSERT_EQ(signals, MX_JOB_NO_PROCESSES | MX_JOB_NO_JOBS, "");

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
    ASSERT_EQ(signals, MX_JOB_NO_JOBS, "");

    mx_nanosleep(mx_deadline_after(MX_MSEC(5)));
    ASSERT_EQ(mx_task_kill(process), NO_ERROR, "");

    ASSERT_EQ(mx_object_wait_one(
        job_child, MX_JOB_NO_PROCESSES, MX_TIME_INFINITE, &signals), NO_ERROR, "");
    ASSERT_EQ(signals, MX_JOB_NO_PROCESSES | MX_JOB_NO_JOBS, "");

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

BEGIN_TEST_CASE(job_tests)
RUN_TEST(basic_test)
RUN_TEST(create_test)
RUN_TEST(kill_test)
RUN_TEST(wait_test)
RUN_TEST(info_task_stats_fails)
END_TEST_CASE(job_tests)
