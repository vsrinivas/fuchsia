// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>

#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>
#include <magenta/types.h>

#include <mini-process/mini-process.h>

#include <unittest/unittest.h>

namespace {

const mx_time_t kTimeoutNs = MX_MSEC(250);

bool mini_process_sanity() {
    BEGIN_TEST;

    mx_handle_t proc;
    mx_handle_t thread;
    mx_handle_t vmar;

    ASSERT_EQ(mx_process_create(mx_job_default(), "mini-p", 3u, 0, &proc, &vmar), MX_OK);
    ASSERT_EQ(mx_thread_create(proc, "mini-p", 2u, 0u, &thread), MX_OK);

    mx_handle_t event;
    ASSERT_EQ(mx_event_create(0u, &event), MX_OK);

    mx_handle_t cmd_channel;
    EXPECT_EQ(start_mini_process_etc(proc, thread, vmar, event, &cmd_channel), MX_OK);

    EXPECT_EQ(mini_process_cmd(cmd_channel, MINIP_CMD_ECHO_MSG, nullptr), MX_OK);

    mx_handle_t oev;
    EXPECT_EQ(mini_process_cmd(cmd_channel, MINIP_CMD_CREATE_EVENT, &oev), MX_OK);

    EXPECT_EQ(mini_process_cmd(cmd_channel, MINIP_CMD_EXIT_NORMAL, nullptr), MX_ERR_PEER_CLOSED);

    mx_handle_close(thread);
    mx_handle_close(proc);
    mx_handle_close(vmar);
    END_TEST;
}

bool process_start_fail() {
    BEGIN_TEST;

    mx_handle_t event1, event2;
    mx_handle_t process;
    mx_handle_t thread;

    ASSERT_EQ(mx_event_create(0u, &event1), MX_OK);
    ASSERT_EQ(mx_event_create(0u, &event2), MX_OK);

    ASSERT_EQ(start_mini_process(mx_job_default(), event1, &process, &thread), MX_OK);

    mx_handle_t other_thread;
    ASSERT_EQ(mx_thread_create(process, "test", 4u, 0, &other_thread), MX_OK);

    // Test that calling process_start() again for an existing process fails in a
    // reasonable way. Also test that the transfered object is back into this process.
    EXPECT_EQ(mx_process_start(process, other_thread, 0, 0, event2, 0), MX_ERR_BAD_STATE);
    EXPECT_EQ(mx_object_signal(event2, 0u, MX_EVENT_SIGNALED), MX_OK);

    mx_handle_close(event2);
    mx_handle_close(process);
    mx_handle_close(thread);
    mx_handle_close(other_thread);
    END_TEST;
}

bool kill_process_via_thread_close() {
    BEGIN_TEST;

    mx_handle_t event;
    ASSERT_EQ(mx_event_create(0u, &event), MX_OK);

    mx_handle_t process;
    mx_handle_t thread;
    ASSERT_EQ(start_mini_process(mx_job_default(), event, &process, &thread), MX_OK);

    // closing the only thread handle should cause the process to terminate.
    EXPECT_EQ(mx_handle_close(thread), MX_OK);

    mx_signals_t signals;
    EXPECT_EQ(mx_object_wait_one(
        process, MX_TASK_TERMINATED, MX_TIME_INFINITE, &signals), MX_OK);
    EXPECT_EQ(signals, MX_TASK_TERMINATED | MX_SIGNAL_LAST_HANDLE);

    EXPECT_EQ(mx_handle_close(process), MX_OK);
    END_TEST;
}

bool kill_process_via_process_close() {
    BEGIN_TEST;

    mx_handle_t event;
    ASSERT_EQ(mx_event_create(0u, &event), MX_OK);

    mx_handle_t process;
    mx_handle_t thread;
    ASSERT_EQ(start_mini_process(mx_job_default(), event, &process, &thread), MX_OK);

    // closing the only process handle should cause the process to terminate.
    EXPECT_EQ(mx_handle_close(process), MX_OK);

    mx_signals_t signals;
    EXPECT_EQ(mx_object_wait_one(
        thread, MX_TASK_TERMINATED, MX_TIME_INFINITE, &signals), MX_OK);
    EXPECT_EQ(signals, MX_TASK_TERMINATED | MX_SIGNAL_LAST_HANDLE);

    EXPECT_EQ(mx_handle_close(thread), MX_OK);
    END_TEST;
}

bool kill_process_via_thread_kill() {
    BEGIN_TEST;

    mx_handle_t event;
    ASSERT_EQ(mx_event_create(0u, &event), MX_OK);

    mx_handle_t process;
    mx_handle_t thread;
    ASSERT_EQ(start_mini_process(mx_job_default(), event, &process, &thread), MX_OK);

    // Killing the only thread should cause the process to terminate.
    EXPECT_EQ(mx_task_kill(thread), MX_OK);

    mx_signals_t signals;
    EXPECT_EQ(mx_object_wait_one(
        process, MX_TASK_TERMINATED, MX_TIME_INFINITE, &signals), MX_OK);
    EXPECT_EQ(signals, MX_TASK_TERMINATED | MX_SIGNAL_LAST_HANDLE);

    EXPECT_EQ(mx_handle_close(process), MX_OK);
    EXPECT_EQ(mx_handle_close(thread), MX_OK);
    END_TEST;
}

bool kill_process_via_vmar_destroy() {
    BEGIN_TEST;

    mx_handle_t event;
    ASSERT_EQ(mx_event_create(0u, &event), MX_OK);

    mx_handle_t proc;
    mx_handle_t vmar;
    ASSERT_EQ(mx_process_create(mx_job_default(), "ttp", 3u, 0, &proc, &vmar), MX_OK);

    mx_handle_t thread;
    ASSERT_EQ(mx_thread_create(proc, "th", 2u, 0u, &thread), MX_OK);

    // Make the process busy-wait rather than using a vDSO call because
    // if it maps in the vDSO then mx_vmar_destroy is prohibited.
    EXPECT_EQ(start_mini_process_etc(proc, thread, vmar, event, nullptr),
              MX_OK);

    // Destroying the root VMAR should cause the process to terminate.
    REGISTER_CRASH(proc);
    EXPECT_EQ(mx_vmar_destroy(vmar), MX_OK);

    mx_signals_t signals;
    EXPECT_EQ(mx_object_wait_one(
        proc, MX_TASK_TERMINATED, MX_TIME_INFINITE, &signals), MX_OK);
    signals &= MX_TASK_TERMINATED;
    EXPECT_EQ(signals, MX_TASK_TERMINATED);

    EXPECT_EQ(mx_handle_close(proc), MX_OK);
    EXPECT_EQ(mx_handle_close(vmar), MX_OK);
    EXPECT_EQ(mx_handle_close(thread), MX_OK);
    END_TEST;
}

bool kill_process_handle_cycle() {
    BEGIN_TEST;

    mx_handle_t proc1, proc2;
    mx_handle_t vmar1, vmar2;

    ASSERT_EQ(mx_process_create(mx_job_default(), "ttp1", 4u, 0u, &proc1, &vmar1), MX_OK);
    ASSERT_EQ(mx_process_create(mx_job_default(), "ttp2", 4u, 0u, &proc2, &vmar2), MX_OK);

    mx_handle_t thread1, thread2;

    ASSERT_EQ(mx_thread_create(proc1, "th1", 3u, 0u, &thread1), MX_OK);
    ASSERT_EQ(mx_thread_create(proc2, "th2", 3u, 0u, &thread2), MX_OK);

    mx_handle_t dup1, dup2;

    EXPECT_EQ(mx_handle_duplicate(proc1, MX_RIGHT_SAME_RIGHTS, &dup1), MX_OK);
    EXPECT_EQ(mx_handle_duplicate(proc2, MX_RIGHT_SAME_RIGHTS, &dup2), MX_OK);

    mx_handle_t minip_chn[2];

    EXPECT_EQ(start_mini_process_etc(proc1, thread1, vmar1, dup2, &minip_chn[0]),
              MX_OK);
    EXPECT_EQ(start_mini_process_etc(proc2, thread2, vmar2, dup1, &minip_chn[1]),
              MX_OK);

    EXPECT_EQ(mx_handle_close(vmar2), MX_OK);
    EXPECT_EQ(mx_handle_close(vmar1), MX_OK);

    EXPECT_EQ(mx_handle_close(proc1), MX_OK);
    EXPECT_EQ(mx_handle_close(proc2), MX_OK);

    // At this point each processes have each other last process handle.  Make sure
    // they are running.

    mx_signals_t signals;
    EXPECT_EQ(mx_object_wait_one(
        thread1, MX_TASK_TERMINATED, mx_deadline_after(kTimeoutNs), &signals), MX_ERR_TIMED_OUT);

    EXPECT_EQ(mx_object_wait_one(
        thread2, MX_TASK_TERMINATED, mx_deadline_after(kTimeoutNs), &signals), MX_ERR_TIMED_OUT);

    EXPECT_EQ(mx_handle_close(thread1), MX_OK);

    // Closing thread1 should cause process 1 to exit which should cause process 2 to
    // exit which we test by waiting on the second process thread handle.

    EXPECT_EQ(mx_object_wait_one(
        thread2, MX_TASK_TERMINATED, MX_TIME_INFINITE, &signals), MX_OK);
    EXPECT_EQ(signals, MX_TASK_TERMINATED | MX_SIGNAL_LAST_HANDLE);

    EXPECT_EQ(mx_handle_close(thread2), MX_OK);

    END_TEST;
}

static mx_status_t dup_send_handle(mx_handle_t channel, mx_handle_t handle) {
    mx_handle_t dup;
    mx_status_t st = mx_handle_duplicate(handle, MX_RIGHT_SAME_RIGHTS, &dup);
    if (st < 0)
        return st;
    return mx_channel_write(channel, 0u, nullptr, 0u, &dup, 1u);
}

bool kill_channel_handle_cycle() {
    BEGIN_TEST;

    mx_handle_t chan[2] = {MX_HANDLE_INVALID, MX_HANDLE_INVALID};
    ASSERT_EQ(mx_channel_create(0u, &chan[0], &chan[1]), MX_OK);

    mx_handle_t proc1, proc2;
    mx_handle_t vmar1, vmar2;

    mx_handle_t job_child;
    ASSERT_EQ(mx_job_create(mx_job_default(), 0u, &job_child), MX_OK);

    ASSERT_EQ(mx_process_create(job_child, "ttp1", 4u, 0u, &proc1, &vmar1), MX_OK);
    ASSERT_EQ(mx_process_create(job_child, "ttp2", 4u, 0u, &proc2, &vmar2), MX_OK);

    mx_handle_t thread1, thread2;

    ASSERT_EQ(mx_thread_create(proc1, "th1", 3u, 0u, &thread1), MX_OK);
    ASSERT_EQ(mx_thread_create(proc2, "th2", 3u, 0u, &thread2), MX_OK);

    // Now we stuff duplicated process and thread handles into each side of the channel.

    EXPECT_EQ(dup_send_handle(chan[0], proc2), MX_OK);
    EXPECT_EQ(dup_send_handle(chan[0], thread2), MX_OK);

    EXPECT_EQ(dup_send_handle(chan[1], proc1), MX_OK);
    EXPECT_EQ(dup_send_handle(chan[1], thread1), MX_OK);

    // The process start with each one side of the channel. We don't have access to the
    // channel anymore.

    mx_handle_t minip_chn[2];

    EXPECT_EQ(start_mini_process_etc(proc1, thread1, vmar1, chan[0], &minip_chn[0]),
              MX_OK);
    EXPECT_EQ(start_mini_process_etc(proc2, thread2, vmar2, chan[1], &minip_chn[1]),
              MX_OK);

    EXPECT_EQ(mx_handle_close(vmar2), MX_OK);
    EXPECT_EQ(mx_handle_close(vmar1), MX_OK);

    EXPECT_EQ(mx_handle_close(proc1), MX_OK);
    EXPECT_EQ(mx_handle_close(proc2), MX_OK);

    // Make (relatively) certain the processes are alive.

    mx_signals_t signals;
    EXPECT_EQ(mx_object_wait_one(
        thread1, MX_TASK_TERMINATED, mx_deadline_after(kTimeoutNs), &signals), MX_ERR_TIMED_OUT);

    EXPECT_EQ(mx_object_wait_one(
        thread2, MX_TASK_TERMINATED, mx_deadline_after(kTimeoutNs), &signals), MX_ERR_TIMED_OUT);

    // At this point the two processes have each other thread/process handles. For example
    // if we close the thread handles, unlike the previous test, the processes will
    // still be alive.

    EXPECT_EQ(mx_handle_close(thread1), MX_OK);

    EXPECT_EQ(mx_object_wait_one(
        thread2, MX_TASK_TERMINATED, mx_deadline_after(kTimeoutNs), &signals), MX_ERR_TIMED_OUT);

    // The only way out of this situation is to use the job handle.

    EXPECT_EQ(mx_task_kill(job_child), MX_OK);

    EXPECT_EQ(mx_object_wait_one(
        thread2, MX_TASK_TERMINATED, MX_TIME_INFINITE, &signals), MX_OK);
    signals &= MX_TASK_TERMINATED;
    EXPECT_EQ(signals, MX_TASK_TERMINATED);

    EXPECT_EQ(mx_handle_close(thread2), MX_OK);
    EXPECT_EQ(mx_handle_close(job_child), MX_OK);

    END_TEST;
}

// Tests that |mx_info_process_t| fields reflect the current state of a process.
bool info_reflects_process_state() {
    BEGIN_TEST;

    // Create a process with one thread.
    mx_handle_t event;
    ASSERT_EQ(mx_event_create(0u, &event), MX_OK);

    mx_handle_t job_child;
    ASSERT_EQ(mx_job_create(mx_job_default(), 0u, &job_child), MX_OK);

    mx_handle_t proc;
    mx_handle_t vmar;
    ASSERT_EQ(mx_process_create(job_child, "ttp", 4u, 0u, &proc, &vmar), MX_OK);

    mx_handle_t thread;
    ASSERT_EQ(mx_thread_create(proc, "th", 3u, 0u, &thread), MX_OK);

    mx_info_process_t info;
    ASSERT_EQ(mx_object_get_info(
            proc, MX_INFO_PROCESS, &info, sizeof(info), NULL, NULL), MX_OK);
    EXPECT_FALSE(info.started, "process should not appear as started");
    EXPECT_FALSE(info.exited, "process should not appear as exited");

    mx_handle_t minip_chn;
    // Start the process and make (relatively) certain it's alive.
    ASSERT_EQ(start_mini_process_etc(proc, thread, vmar, event, &minip_chn),
              MX_OK);
    mx_signals_t signals;
    ASSERT_EQ(mx_object_wait_one(
        proc, MX_TASK_TERMINATED, mx_deadline_after(kTimeoutNs), &signals), MX_ERR_TIMED_OUT);

    ASSERT_EQ(mx_object_get_info(
            proc, MX_INFO_PROCESS, &info, sizeof(info), NULL, NULL), MX_OK);
    EXPECT_TRUE(info.started, "process should appear as started");
    EXPECT_FALSE(info.exited, "process should not appear as exited");

    // Kill the process and wait for it to terminate.
    ASSERT_EQ(mx_task_kill(proc), MX_OK);
    ASSERT_EQ(mx_object_wait_one(
        proc, MX_TASK_TERMINATED, MX_TIME_INFINITE, &signals), MX_OK);
    ASSERT_EQ(signals, MX_TASK_TERMINATED | MX_SIGNAL_LAST_HANDLE);

    ASSERT_EQ(mx_object_get_info(
            proc, MX_INFO_PROCESS, &info, sizeof(info), NULL, NULL), MX_OK);
    EXPECT_TRUE(info.started, "process should appear as started");
    EXPECT_TRUE(info.exited, "process should appear as exited");
    EXPECT_NE(info.return_code, 0, "killed process should have non-zero return code");

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(process_tests)
RUN_TEST(mini_process_sanity);
RUN_TEST(process_start_fail);
RUN_TEST(kill_process_via_thread_close);
RUN_TEST(kill_process_via_process_close);
RUN_TEST(kill_process_via_thread_kill);
RUN_TEST_ENABLE_CRASH_HANDLER(kill_process_via_vmar_destroy);
RUN_TEST(kill_process_handle_cycle);
RUN_TEST(kill_channel_handle_cycle);
RUN_TEST(info_reflects_process_state);
END_TEST_CASE(process_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
#endif
