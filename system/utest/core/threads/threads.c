// Copyright 2016 The Fuchsia Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <unistd.h>

#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/exception.h>
#include <magenta/syscalls/object.h>
#include <magenta/syscalls/port.h>

#include <unittest/unittest.h>
#include <runtime/thread.h>

static const char kThreadName[] = "test-thread";

static const unsigned kExceptionPortKey = 42u;

__NO_SAFESTACK static void test_sleep_thread_fn(void* arg) {
    // Note: You shouldn't use C standard library functions from this thread.
    mx_time_t time = (mx_time_t)arg;
    mx_nanosleep(time);
    mx_thread_exit();
}

__NO_SAFESTACK static void test_wait_thread_fn(void* arg) {
    mx_handle_t event = *(mx_handle_t*)arg;
    mx_object_wait_one(event, MX_USER_SIGNAL_0, MX_TIME_INFINITE, NULL);
    mx_object_signal(event, 0u, MX_USER_SIGNAL_1);
    mx_thread_exit();
}

__NO_SAFESTACK static void busy_thread_fn(void* arg) {
    volatile uint64_t i = 0u;
    while (true) {
        ++i;
    }
    __builtin_trap();
}

__NO_SAFESTACK static void sleep_thread_fn(void* arg) {
    mx_nanosleep(MX_TIME_INFINITE);
    __builtin_trap();
}

__NO_SAFESTACK static void wait_thread_fn(void* arg) {
    mx_handle_t event = *(mx_handle_t*)arg;
    mx_object_wait_one(event, MX_USER_SIGNAL_0, MX_TIME_INFINITE, NULL);
    __builtin_trap();
}

__NO_SAFESTACK static void test_port_thread_fn(void* arg) {
    mx_handle_t* port = (mx_handle_t*)arg;
    mx_port_packet_t packet = {};
    mx_port_wait(port[0], MX_TIME_INFINITE, &packet, 0u);
    packet.key += 5u;
    mx_port_queue(port[1], &packet, 0u);
    mx_thread_exit();
}

static bool start_thread(mxr_thread_entry_t entry, void* arg,
                         mxr_thread_t* thread_out) {
    const size_t stack_size = 256u << 10;
    mx_handle_t thread_stack_vmo;
    ASSERT_EQ(mx_vmo_create(stack_size, 0, &thread_stack_vmo), NO_ERROR, "");
    ASSERT_GT(thread_stack_vmo, 0, "");

    uintptr_t stack = 0u;
    ASSERT_EQ(mx_vmar_map(mx_vmar_root_self(), 0, thread_stack_vmo, 0, stack_size,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, &stack), NO_ERROR, "");
    ASSERT_EQ(mx_handle_close(thread_stack_vmo), NO_ERROR, "");

    ASSERT_EQ(mxr_thread_create(mx_process_self(), "test_thread", false,
                                thread_out),
              NO_ERROR, "");
    ASSERT_EQ(mxr_thread_start(thread_out, stack, stack_size, entry, arg),
              NO_ERROR, "");
    return true;
}

static bool start_and_kill_thread(mxr_thread_entry_t entry, void* arg) {
    mxr_thread_t thread;
    ASSERT_TRUE(start_thread(entry, arg, &thread), "");
    mx_nanosleep(mx_deadline_after(MX_MSEC(100)));
    ASSERT_EQ(mxr_thread_kill(&thread), NO_ERROR, "");
    ASSERT_EQ(mxr_thread_join(&thread), NO_ERROR, "");
    return true;
}

static bool set_debugger_exception_port(mx_handle_t* eport_out) {
    ASSERT_EQ(mx_port_create(0, eport_out), NO_ERROR, "");
    mx_handle_t self = mx_process_self();
    ASSERT_EQ(mx_task_bind_exception_port(self, *eport_out, kExceptionPortKey,
                                          MX_EXCEPTION_PORT_DEBUGGER),
              NO_ERROR, "");
    return true;
}

static bool test_basics(void) {
    BEGIN_TEST;
    mxr_thread_t thread;
    ASSERT_TRUE(start_thread(test_sleep_thread_fn, (void*)mx_deadline_after(MX_MSEC(100)),
                             &thread), "");
    ASSERT_EQ(mx_object_wait_one(mxr_thread_get_handle(&thread),
                                 MX_THREAD_TERMINATED, MX_TIME_INFINITE, NULL),
              NO_ERROR, "");
    END_TEST;
}

static bool test_long_name_succeeds(void) {
    BEGIN_TEST;
    // Creating a thread with a super long name should succeed.
    static const char long_name[] =
        "0123456789012345678901234567890123456789"
        "0123456789012345678901234567890123456789";
    ASSERT_GT(strlen(long_name), (size_t)MX_MAX_NAME_LEN-1,
              "too short to truncate");

    mxr_thread_t thread;
    ASSERT_EQ(mxr_thread_create(mx_process_self(), long_name, false, &thread),
              NO_ERROR, "");
    mxr_thread_destroy(&thread);
    END_TEST;
}

// mx_thread_start() is not supposed to be usable for creating a
// process's first thread.  That's what mx_process_start() is for.
// Check that mx_thread_start() returns an error in this case.
static bool test_thread_start_on_initial_thread(void) {
    BEGIN_TEST;

    static const char kProcessName[] = "test-proc-thread1";
    mx_handle_t process;
    mx_handle_t vmar;
    mx_handle_t thread;
    ASSERT_EQ(mx_process_create(mx_job_default(), kProcessName, sizeof(kProcessName) - 1,
                                0, &process, &vmar), NO_ERROR, "");
    ASSERT_EQ(mx_thread_create(process, kThreadName, sizeof(kThreadName) - 1,
                               0, &thread), NO_ERROR, "");
    ASSERT_EQ(mx_thread_start(thread, 1, 1, 1, 1), ERR_BAD_STATE, "");

    ASSERT_EQ(mx_handle_close(thread), NO_ERROR, "");
    ASSERT_EQ(mx_handle_close(vmar), NO_ERROR, "");
    ASSERT_EQ(mx_handle_close(process), NO_ERROR, "");

    END_TEST;
}

// Test that we don't get an assertion failure (and kernel panic) if we
// pass a zero instruction pointer when starting a thread (in this case via
// mx_process_start()).
static bool test_thread_start_with_zero_instruction_pointer(void) {
    BEGIN_TEST;

    static const char kProcessName[] = "test-proc-thread2";
    mx_handle_t process;
    mx_handle_t vmar;
    mx_handle_t thread;
    ASSERT_EQ(mx_process_create(mx_job_default(), kProcessName, sizeof(kProcessName) - 1,
                                0, &process, &vmar), NO_ERROR, "");
    ASSERT_EQ(mx_thread_create(process, kThreadName, sizeof(kThreadName) - 1,
                               0, &thread), NO_ERROR, "");
    ASSERT_EQ(mx_process_start(process, thread, 0, 0, thread, 0), NO_ERROR, "");

    // Give crashlogger a little time to print info about the new thread
    // (since it will start and crash), otherwise that output gets
    // interleaved with the test runner's output.
    mx_nanosleep(mx_deadline_after(MX_MSEC(100)));

    ASSERT_EQ(mx_handle_close(process), NO_ERROR, "");
    ASSERT_EQ(mx_handle_close(vmar), NO_ERROR, "");

    END_TEST;
}

static bool test_kill_busy_thread(void) {
    BEGIN_TEST;

    ASSERT_TRUE(start_and_kill_thread(busy_thread_fn, NULL), "");

    END_TEST;
}

static bool test_kill_sleep_thread(void) {
    BEGIN_TEST;

    ASSERT_TRUE(start_and_kill_thread(sleep_thread_fn, NULL), "");

    END_TEST;
}

static bool test_kill_wait_thread(void) {
    BEGIN_TEST;

    mx_handle_t event;
    ASSERT_EQ(mx_event_create(0, &event), NO_ERROR, "");
    ASSERT_TRUE(start_and_kill_thread(wait_thread_fn, &event), "");
    ASSERT_EQ(mx_handle_close(event), NO_ERROR, "");

    END_TEST;
}

static bool test_info_task_stats_fails(void) {
    BEGIN_TEST;
    // Spin up a thread.
    mxr_thread_t thread;
    ASSERT_TRUE(start_thread(test_sleep_thread_fn, (void*)mx_deadline_after(MX_MSEC(100)), &thread),
                "");
    mx_handle_t thandle = mxr_thread_get_handle(&thread);
    ASSERT_EQ(mx_object_wait_one(thandle,
                                 MX_THREAD_TERMINATED, MX_TIME_INFINITE, NULL),
              NO_ERROR, "");

    // Ensure that task_stats doesn't work on it.
    mx_info_task_stats_t info;
    EXPECT_NEQ(mx_object_get_info(thandle, MX_INFO_TASK_STATS,
                                  &info, sizeof(info), NULL, NULL),
               NO_ERROR,
               "Just added thread support to info_task_status?");
    // If so, replace this with a real test; see example in process.cpp.

    ASSERT_EQ(mx_handle_close(thandle), NO_ERROR, "");
    END_TEST;
}

static bool test_resume_suspended(void) {
    BEGIN_TEST;

    mx_handle_t event;
    mxr_thread_t thread;

    ASSERT_EQ(mx_event_create(0, &event), NO_ERROR, "");
    ASSERT_TRUE(start_thread(test_wait_thread_fn, &event, &thread), "");
    mx_handle_t thread_h = mxr_thread_get_handle(&thread);
    ASSERT_EQ(mx_task_suspend(thread_h), NO_ERROR, "");
    ASSERT_EQ(mx_task_resume(thread_h, 0), NO_ERROR, "");

    // The thread should still be blocked on the event when it wakes up
    ASSERT_EQ(mx_object_wait_one(thread_h, MX_THREAD_TERMINATED, mx_deadline_after(MX_MSEC(100)),
                                 NULL), ERR_TIMED_OUT, "");

    // Verify thread is blocked
    mx_info_thread_t info;
    ASSERT_EQ(mx_object_get_info(thread_h, MX_INFO_THREAD,
                                 &info, sizeof(info), NULL, NULL),
              NO_ERROR, "");
    ASSERT_EQ(info.wait_exception_port_type, MX_EXCEPTION_PORT_TYPE_NONE, "");
    ASSERT_EQ(info.state, MX_THREAD_STATE_BLOCKED, "");

    // Attach to debugger port so we can see MX_EXCP_THREAD_SUSPENDED
    mx_handle_t eport;
    ASSERT_TRUE(set_debugger_exception_port(&eport),"");

    // Check that signaling the event while suspended results in the expected
    // behavior
    ASSERT_EQ(mx_task_suspend(thread_h), NO_ERROR, "");

    // Wait for the thread to suspend
    mx_exception_packet_t packet;
    ASSERT_EQ(mx_port_wait(eport, mx_deadline_after(MX_SEC(1)), &packet, sizeof(packet)), NO_ERROR, "");
    ASSERT_EQ(packet.hdr.key, kExceptionPortKey, "");
    ASSERT_EQ(packet.report.header.type, (uint32_t) MX_EXCP_THREAD_SUSPENDED, "");

    // Verify thread is suspended
    ASSERT_EQ(mx_object_get_info(thread_h, MX_INFO_THREAD,
                                 &info, sizeof(info), NULL, NULL),
              NO_ERROR, "");
    ASSERT_EQ(info.state, MX_THREAD_STATE_SUSPENDED, "");
    ASSERT_EQ(info.wait_exception_port_type, MX_EXCEPTION_PORT_TYPE_NONE, "");

    // Since the thread is suspended the signaling should not take effect.
    ASSERT_EQ(mx_object_signal(event, 0, MX_USER_SIGNAL_0), NO_ERROR, "");
    ASSERT_EQ(mx_object_wait_one(event, MX_USER_SIGNAL_1, mx_deadline_after(MX_MSEC(100)), NULL), ERR_TIMED_OUT, "");

    ASSERT_EQ(mx_task_resume(thread_h, 0), NO_ERROR, "");

    ASSERT_EQ(mx_object_wait_one(event, MX_USER_SIGNAL_1, MX_TIME_INFINITE, NULL), NO_ERROR, "");
    ASSERT_EQ(mx_object_wait_one(
        thread_h, MX_THREAD_TERMINATED, MX_TIME_INFINITE, NULL), NO_ERROR, "");

    ASSERT_EQ(mx_handle_close(eport), NO_ERROR, "");
    ASSERT_EQ(mx_handle_close(event), NO_ERROR, "");
    ASSERT_EQ(mx_handle_close(thread_h), NO_ERROR, "");

    END_TEST;
}

static bool test_kill_suspended(void) {
    BEGIN_TEST;

    mx_handle_t event;
    mxr_thread_t thread;

    ASSERT_EQ(mx_event_create(0, &event), NO_ERROR, "");
    ASSERT_TRUE(start_thread(test_wait_thread_fn, &event, &thread), "");
    mx_handle_t thread_h = mxr_thread_get_handle(&thread);
    ASSERT_EQ(mx_task_suspend(thread_h), NO_ERROR, "");
    mx_nanosleep(mx_deadline_after(MX_MSEC(10)));
    ASSERT_EQ(mx_task_kill(thread_h), NO_ERROR, "");

    ASSERT_EQ(mx_object_wait_one(
        thread_h, MX_THREAD_TERMINATED, MX_TIME_INFINITE, NULL), NO_ERROR, "");

    // make sure the thread did not execute more user code.
    ASSERT_EQ(mx_object_wait_one(event, MX_USER_SIGNAL_1, mx_deadline_after(MX_MSEC(100)), NULL), ERR_TIMED_OUT, "");

    ASSERT_EQ(mx_handle_close(event), NO_ERROR, "");
    ASSERT_EQ(mx_handle_close(thread_h), NO_ERROR, "");

    END_TEST;
}

static bool test_suspend_sleeping(void) {
    BEGIN_TEST;

    const mx_time_t sleep_deadline = mx_deadline_after(MX_MSEC(100));
    mxr_thread_t thread;

    // TODO(teisenbe): This code could be made less racy with a deadline sleep
    // mode when we get one.
    ASSERT_TRUE(start_thread(test_sleep_thread_fn, (void*)sleep_deadline, &thread), "");
    mx_handle_t thread_h = mxr_thread_get_handle(&thread);

    mx_nanosleep(sleep_deadline - MX_MSEC(50));
    ASSERT_EQ(mx_task_suspend(thread_h), NO_ERROR, "");

    // TODO(teisenbe): Once we wire in exceptions for suspend, check here that
    // we receive it.
    mx_nanosleep(sleep_deadline - MX_MSEC(50));

    ASSERT_EQ(mx_task_resume(thread_h, 0), NO_ERROR, "");

    // Wait for the sleep to finish
    ASSERT_EQ(mx_object_wait_one(thread_h, MX_THREAD_TERMINATED, sleep_deadline + MX_MSEC(50), NULL),
              NO_ERROR, "");
    const mx_time_t now = mx_time_get(MX_CLOCK_MONOTONIC);
    ASSERT_GE(now, sleep_deadline, "thread did not sleep long enough");

    ASSERT_EQ(mx_handle_close(thread_h), NO_ERROR, "");
    END_TEST;
}

struct channel_call_suspend_test_arg {
    mx_handle_t channel;
    mx_status_t call_status;
    mx_status_t read_status;
};

__NO_SAFESTACK static void test_channel_call_thread_fn(void* arg_) {
    struct channel_call_suspend_test_arg* arg = arg_;

    uint8_t send_buf[9] = "abcdefghi";
    uint8_t recv_buf[9];
    uint32_t actual_bytes, actual_handles;

    mx_channel_call_args_t call_args = {
        .wr_bytes = send_buf,
        .wr_handles = NULL,
        .rd_bytes = recv_buf,
        .rd_handles = NULL,
        .wr_num_bytes = sizeof(send_buf),
        .wr_num_handles = 0,
        .rd_num_bytes = sizeof(recv_buf),
        .rd_num_handles = 0,
    };

    arg->read_status = NO_ERROR;
    arg->call_status = mx_channel_call(arg->channel, 0, MX_TIME_INFINITE, &call_args,
                                       &actual_bytes, &actual_handles, &arg->read_status);

    if (arg->call_status == NO_ERROR) {
        arg->read_status = NO_ERROR;
        if (actual_bytes != sizeof(recv_buf) || memcmp(recv_buf, "abcdefghj", sizeof(recv_buf))) {
            arg->call_status = ERR_BAD_STATE;
        }
    }

    mx_handle_close(arg->channel);
    mx_thread_exit();
}

static bool test_suspend_channel_call(void) {
    BEGIN_TEST;

    mxr_thread_t thread;

    mx_handle_t channel;
    struct channel_call_suspend_test_arg thread_arg;
    ASSERT_EQ(mx_channel_create(0, &thread_arg.channel, &channel), NO_ERROR, "");
    thread_arg.call_status = ERR_BAD_STATE;

    ASSERT_TRUE(start_thread(test_channel_call_thread_fn, &thread_arg, &thread), "");
    mx_handle_t thread_h = mxr_thread_get_handle(&thread);

    // Wait for the thread to send a channel call before suspending it
    ASSERT_EQ(mx_object_wait_one(channel, MX_CHANNEL_READABLE, MX_TIME_INFINITE, NULL),
              NO_ERROR, "");

    ASSERT_EQ(mx_task_suspend(thread_h), NO_ERROR, "");
    // TODO(teisenbe): Once we wire in exceptions for suspend, check here that
    // we receive it.

    // Read the message
    uint8_t buf[9];
    uint32_t actual_bytes;
    ASSERT_EQ(mx_channel_read(channel, 0, buf, NULL, sizeof(buf), 0, &actual_bytes, NULL),
              NO_ERROR, "");
    ASSERT_EQ(actual_bytes, sizeof(buf), "");
    ASSERT_EQ(memcmp(buf, "abcdefghi", sizeof(buf)), 0, "");

    // Write a reply
    buf[8] = 'j';
    ASSERT_EQ(mx_channel_write(channel, 0, buf, sizeof(buf), NULL, 0), NO_ERROR, "");

    // Make sure the remote channel didn't get signaled
    EXPECT_EQ(mx_object_wait_one(thread_arg.channel, MX_CHANNEL_READABLE, 0, NULL),
              ERR_TIMED_OUT, "");

    // Make sure we can't read from the remote channel (the message should have
    // been reserved for the other thread, even though it is suspended).
    EXPECT_EQ(mx_channel_read(thread_arg.channel, 0, buf, NULL, sizeof(buf), 0,
                              &actual_bytes, NULL),
              ERR_SHOULD_WAIT, "");

    // Wake the suspended thread
    ASSERT_EQ(mx_task_resume(thread_h, 0), NO_ERROR, "");

    // Wait for the thread to finish
    ASSERT_EQ(mx_object_wait_one(thread_h, MX_THREAD_TERMINATED, MX_TIME_INFINITE, NULL),
              NO_ERROR, "");
    EXPECT_EQ(thread_arg.call_status, NO_ERROR, "");
    EXPECT_EQ(thread_arg.read_status, NO_ERROR, "");

    ASSERT_EQ(mx_handle_close(channel), NO_ERROR, "");

    END_TEST;
}

static bool test_suspend_port_call(void) {
    BEGIN_TEST;

    mxr_thread_t thread;
    mx_handle_t port[2];
    ASSERT_EQ(mx_port_create(MX_PORT_OPT_V2, &port[0]), NO_ERROR, "");
    ASSERT_EQ(mx_port_create(MX_PORT_OPT_V2, &port[1]), NO_ERROR, "");

    ASSERT_TRUE(start_thread(test_port_thread_fn, port, &thread), "");
    mx_handle_t thread_h = mxr_thread_get_handle(&thread);

    mx_nanosleep(mx_deadline_after(MX_MSEC(100)));
    ASSERT_EQ(mx_task_suspend(thread_h), NO_ERROR, "");

    mx_port_packet_t packet1 = { 100ull, MX_PKT_TYPE_USER, 0u, {} };
    mx_port_packet_t packet2 = { 300ull, MX_PKT_TYPE_USER, 0u, {} };

    ASSERT_EQ(mx_port_queue(port[0], &packet1, 0u), NO_ERROR, "");
    ASSERT_EQ(mx_port_queue(port[0], &packet2, 0u), NO_ERROR, "");

    mx_port_packet_t packet;
    ASSERT_EQ(mx_port_wait(port[1], mx_deadline_after(MX_MSEC(100)), &packet, 0u), ERR_TIMED_OUT, "");

    ASSERT_EQ(mx_task_resume(thread_h, 0), NO_ERROR, "");

    ASSERT_EQ(mx_port_wait(port[1], MX_TIME_INFINITE, &packet, 0u), NO_ERROR, "");
    EXPECT_EQ(packet.key, 105ull, "");

    ASSERT_EQ(mx_port_wait(port[0], MX_TIME_INFINITE, &packet, 0u), NO_ERROR, "");
    EXPECT_EQ(packet.key, 300ull, "");

    ASSERT_EQ(mx_object_wait_one(
        thread_h, MX_THREAD_TERMINATED, MX_TIME_INFINITE, NULL), NO_ERROR, "");

    ASSERT_EQ(mx_handle_close(thread_h), NO_ERROR, "");
    ASSERT_EQ(mx_handle_close(port[0]), NO_ERROR, "");
    ASSERT_EQ(mx_handle_close(port[1]), NO_ERROR, "");

    END_TEST;
}

struct test_writing_thread_arg {
    volatile int v;
};

__NO_SAFESTACK static void test_writing_thread_fn(void* arg_) {
    struct test_writing_thread_arg* arg = arg_;
    while (true) {
        arg->v = 1;
    }
    __builtin_trap();
}

static bool test_suspend_stops_thread(void) {
    BEGIN_TEST;

    mxr_thread_t thread;

    struct test_writing_thread_arg arg;
    ASSERT_TRUE(start_thread(test_writing_thread_fn, &arg, &thread), "");
    mx_handle_t thread_h = mxr_thread_get_handle(&thread);

    while (arg.v != 1) {
        mx_nanosleep(0);
    }
    ASSERT_EQ(mx_task_suspend(thread_h), NO_ERROR, "");
    while (arg.v != 2) {
        arg.v = 2;
        // Give the thread a chance to clobber the value
        mx_nanosleep(mx_deadline_after(MX_MSEC(50)));
    }
    ASSERT_EQ(mx_task_resume(thread_h, 0), NO_ERROR, "");
    while (arg.v != 1) {
        mx_nanosleep(0);
    }

    ASSERT_EQ(mx_task_kill(thread_h), NO_ERROR, "");
    ASSERT_EQ(mx_handle_close(thread_h), NO_ERROR, "");
    END_TEST;
}

BEGIN_TEST_CASE(threads_tests)
RUN_TEST(test_basics)
RUN_TEST(test_long_name_succeeds)
RUN_TEST(test_thread_start_on_initial_thread)
RUN_TEST(test_thread_start_with_zero_instruction_pointer)
RUN_TEST(test_kill_busy_thread)
RUN_TEST(test_kill_sleep_thread)
RUN_TEST(test_kill_wait_thread)
RUN_TEST(test_info_task_stats_fails)
RUN_TEST(test_resume_suspended)
RUN_TEST(test_kill_suspended)
RUN_TEST(test_suspend_sleeping)
RUN_TEST(test_suspend_channel_call)
RUN_TEST(test_suspend_port_call)
RUN_TEST(test_suspend_stops_thread)
END_TEST_CASE(threads_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
