// Copyright 2016 The Fuchsia Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <unistd.h>

#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/debug.h>
#include <magenta/syscalls/exception.h>
#include <magenta/syscalls/object.h>
#include <magenta/syscalls/port.h>

#include <unittest/unittest.h>
#include <runtime/thread.h>

#include "register-set.h"
#include "test-threads/threads.h"

static const char kThreadName[] = "test-thread";

static const unsigned kExceptionPortKey = 42u;

static bool get_koid(mx_handle_t handle, mx_koid_t* koid) {
    mx_info_handle_basic_t info;
    size_t records_read;
    ASSERT_EQ(mx_object_get_info(
                  handle, MX_INFO_HANDLE_BASIC, &info, sizeof(info),
                  &records_read, NULL), MX_OK, "");
    ASSERT_EQ(records_read, 1u, "");
    *koid = info.koid;
    return true;
}

static bool check_reported_pid_and_tid(mx_handle_t thread,
                                       mx_port_packet_t* packet) {
    mx_koid_t pid;
    mx_koid_t tid;
    if (!get_koid(mx_process_self(), &pid))
        return false;
    if (!get_koid(thread, &tid))
        return false;
    EXPECT_EQ(packet->exception.pid, pid, "");
    EXPECT_EQ(packet->exception.tid, tid, "");
    return true;
}

// Suspend the given thread.  This waits for the thread suspension to take
// effect, using the given exception port.
static bool suspend_thread_synchronous(mx_handle_t thread, mx_handle_t eport) {
    ASSERT_EQ(mx_task_suspend(thread), MX_OK, "");

    // Wait for the thread to suspend.
    for (;;) {
        mx_port_packet_t packet;
        ASSERT_EQ(mx_port_wait(eport, MX_TIME_INFINITE, &packet, 0), MX_OK, "");
        if (packet.type == MX_EXCP_THREAD_EXITING) {
            // Ignore this "thread exiting" event and retry.  This event
            // was probably caused by a thread from an earlier test case.
            // We can get these events even if the previous test case
            // joined the thread or used mx_object_wait_one() to wait for
            // the thread to terminate.
            continue;
        }
        EXPECT_TRUE(check_reported_pid_and_tid(thread, &packet), "");
        ASSERT_EQ(packet.key, kExceptionPortKey, "");
        ASSERT_EQ(packet.type, (uint32_t)MX_EXCP_THREAD_SUSPENDED, "");
        break;
    }

    return true;
}

static bool start_thread(mxr_thread_entry_t entry, void* arg,
                         mxr_thread_t* thread_out, mx_handle_t* thread_h) {
    // TODO: Don't leak these when the thread dies.
    const size_t stack_size = 256u << 10;
    mx_handle_t thread_stack_vmo = MX_HANDLE_INVALID;
    ASSERT_EQ(mx_vmo_create(stack_size, 0, &thread_stack_vmo), MX_OK, "");
    ASSERT_NE(thread_stack_vmo, MX_HANDLE_INVALID, "");

    uintptr_t stack = 0u;
    ASSERT_EQ(mx_vmar_map(mx_vmar_root_self(), 0, thread_stack_vmo, 0, stack_size,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, &stack), MX_OK, "");
    ASSERT_EQ(mx_handle_close(thread_stack_vmo), MX_OK, "");

    ASSERT_EQ(mxr_thread_create(mx_process_self(), "test_thread", false,
                                thread_out),
              MX_OK, "");

    if (thread_h) {
        ASSERT_EQ(mx_handle_duplicate(mxr_thread_get_handle(thread_out), MX_RIGHT_SAME_RIGHTS,
                                      thread_h), MX_OK, "");
    }
    ASSERT_EQ(mxr_thread_start(thread_out, stack, stack_size, entry, arg),
              MX_OK, "");
    return true;
}

static bool start_and_kill_thread(mxr_thread_entry_t entry, void* arg) {
    mxr_thread_t thread;
    mx_handle_t thread_h;
    ASSERT_TRUE(start_thread(entry, arg, &thread, &thread_h), "");
    mx_nanosleep(mx_deadline_after(MX_MSEC(100)));
    ASSERT_EQ(mx_task_kill(thread_h), MX_OK, "");
    ASSERT_EQ(mx_object_wait_one(thread_h, MX_THREAD_TERMINATED,
                                 MX_TIME_INFINITE, NULL),
              MX_OK, "");
    mxr_thread_destroy(&thread);
    ASSERT_EQ(mx_handle_close(thread_h), MX_OK, "");
    return true;
}

static bool set_debugger_exception_port(mx_handle_t* eport_out) {
    ASSERT_EQ(mx_port_create(0, eport_out), MX_OK, "");
    mx_handle_t self = mx_process_self();
    ASSERT_EQ(mx_task_bind_exception_port(self, *eport_out, kExceptionPortKey,
                                          MX_EXCEPTION_PORT_DEBUGGER),
              MX_OK, "");
    return true;
}

static bool test_basics(void) {
    BEGIN_TEST;
    mxr_thread_t thread;
    mx_handle_t thread_h;
    ASSERT_TRUE(start_thread(threads_test_sleep_fn, (void*)mx_deadline_after(MX_MSEC(100)),
                             &thread, &thread_h), "");
    ASSERT_EQ(mx_object_wait_one(thread_h, MX_THREAD_TERMINATED, MX_TIME_INFINITE, NULL),
              MX_OK, "");
    ASSERT_EQ(mx_handle_close(thread_h), MX_OK, "");
    END_TEST;
}

static bool test_detach(void) {
    BEGIN_TEST;
    mxr_thread_t thread;
    mx_handle_t event;
    ASSERT_EQ(mx_event_create(0, &event), MX_OK, "");

    mx_handle_t thread_h;
    ASSERT_TRUE(start_thread(threads_test_wait_detach_fn, &event, &thread, &thread_h), "");
    // We're not detached yet
    ASSERT_FALSE(mxr_thread_detached(&thread), "");

    ASSERT_EQ(mxr_thread_detach(&thread), MX_OK, "");
    ASSERT_TRUE(mxr_thread_detached(&thread), "");

    // Tell thread to exit
    ASSERT_EQ(mx_object_signal(event, 0, MX_USER_SIGNAL_0), MX_OK, "");

    // Wait for thread to exit
    ASSERT_EQ(mx_object_wait_one(thread_h,
                                 MX_THREAD_TERMINATED, MX_TIME_INFINITE, NULL),
              MX_OK, "");

    ASSERT_EQ(mx_handle_close(thread_h), MX_OK, "");

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
              MX_OK, "");
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
                                0, &process, &vmar), MX_OK, "");
    ASSERT_EQ(mx_thread_create(process, kThreadName, sizeof(kThreadName) - 1,
                               0, &thread), MX_OK, "");
    ASSERT_EQ(mx_thread_start(thread, 1, 1, 1, 1), MX_ERR_BAD_STATE, "");

    ASSERT_EQ(mx_handle_close(thread), MX_OK, "");
    ASSERT_EQ(mx_handle_close(vmar), MX_OK, "");
    ASSERT_EQ(mx_handle_close(process), MX_OK, "");

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
                                0, &process, &vmar), MX_OK, "");
    ASSERT_EQ(mx_thread_create(process, kThreadName, sizeof(kThreadName) - 1,
                               0, &thread), MX_OK, "");

    REGISTER_CRASH(process);
    ASSERT_EQ(mx_process_start(process, thread, 0, 0, thread, 0), MX_OK, "");

    mx_signals_t signals;
    EXPECT_EQ(mx_object_wait_one(
        process, MX_TASK_TERMINATED, MX_TIME_INFINITE, &signals), MX_OK, "");
    signals &= MX_TASK_TERMINATED;
    EXPECT_EQ(signals, MX_TASK_TERMINATED, "");

    ASSERT_EQ(mx_handle_close(process), MX_OK, "");
    ASSERT_EQ(mx_handle_close(vmar), MX_OK, "");

    END_TEST;
}

static bool test_kill_busy_thread(void) {
    BEGIN_TEST;

    ASSERT_TRUE(start_and_kill_thread(threads_test_busy_fn, NULL), "");

    END_TEST;
}

static bool test_kill_sleep_thread(void) {
    BEGIN_TEST;

    ASSERT_TRUE(start_and_kill_thread(threads_test_infinite_sleep_fn, NULL), "");

    END_TEST;
}

static bool test_kill_wait_thread(void) {
    BEGIN_TEST;

    mx_handle_t event;
    ASSERT_EQ(mx_event_create(0, &event), MX_OK, "");
    ASSERT_TRUE(start_and_kill_thread(threads_test_infinite_wait_fn, &event), "");
    ASSERT_EQ(mx_handle_close(event), MX_OK, "");

    END_TEST;
}

static bool test_bad_state_nonstarted_thread(void) {
    BEGIN_TEST;

    // perform a bunch of apis against non started threads (in the INITIAL STATE)
    mx_handle_t thread;

    ASSERT_EQ(mx_thread_create(mx_process_self(), "thread", 5, 0, &thread), MX_OK, "");
    ASSERT_EQ(mx_task_resume(thread, 0), MX_ERR_BAD_STATE, "");
    ASSERT_EQ(mx_task_resume(thread, 0), MX_ERR_BAD_STATE, "");
    ASSERT_EQ(mx_handle_close(thread), MX_OK, "");

    ASSERT_EQ(mx_thread_create(mx_process_self(), "thread", 5, 0, &thread), MX_OK, "");
    ASSERT_EQ(mx_task_resume(thread, 0), MX_ERR_BAD_STATE, "");
    ASSERT_EQ(mx_task_suspend(thread), MX_ERR_BAD_STATE, "");
    ASSERT_EQ(mx_handle_close(thread), MX_OK, "");

    ASSERT_EQ(mx_thread_create(mx_process_self(), "thread", 5, 0, &thread), MX_OK, "");
    ASSERT_EQ(mx_task_kill(thread), MX_OK, "");
    ASSERT_EQ(mx_task_kill(thread), MX_OK, "");
    ASSERT_EQ(mx_handle_close(thread), MX_OK, "");

    ASSERT_EQ(mx_thread_create(mx_process_self(), "thread", 5, 0, &thread), MX_OK, "");
    ASSERT_EQ(mx_task_kill(thread), MX_OK, "");
    ASSERT_EQ(mx_task_resume(thread, 0), MX_ERR_BAD_STATE, "");
    ASSERT_EQ(mx_handle_close(thread), MX_OK, "");

    ASSERT_EQ(mx_thread_create(mx_process_self(), "thread", 5, 0, &thread), MX_OK, "");
    ASSERT_EQ(mx_task_kill(thread), MX_OK, "");
    ASSERT_EQ(mx_task_suspend(thread), MX_ERR_BAD_STATE, "");
    ASSERT_EQ(mx_handle_close(thread), MX_OK, "");

    END_TEST;
}

// Arguments for self_killing_fn().
struct self_killing_thread_args {
    mxr_thread_t thread; // Used for the thread to kill itself.
    uint32_t test_value; // Used for testing what the thread does.
};

__NO_SAFESTACK static void self_killing_fn(void* arg) {
    struct self_killing_thread_args* args = arg;
    // Kill the current thread.
    mx_task_kill(mxr_thread_get_handle(&args->thread));
    // We should not reach here -- the syscall should not have returned.
    args->test_value = 999;
    mx_thread_exit();
}

// This tests that the mx_task_kill() syscall does not return when a thread
// uses it to kill itself.
static bool test_thread_kills_itself(void) {
    BEGIN_TEST;

    struct self_killing_thread_args args;
    args.test_value = 111;
    mx_handle_t thread_handle;
    ASSERT_TRUE(start_thread(self_killing_fn, &args, &args.thread, &thread_handle), "");
    ASSERT_EQ(mx_object_wait_one(thread_handle, MX_THREAD_TERMINATED,
                                 MX_TIME_INFINITE, NULL), MX_OK, "");
    ASSERT_EQ(mx_handle_close(thread_handle), MX_OK, "");
    // Check that the thread did not continue execution and modify test_value.
    ASSERT_EQ(args.test_value, 111u, "");
    // We have to destroy the thread afterwards to clean up its internal
    // handle, since it did not properly exit.
    mxr_thread_destroy(&args.thread);

    END_TEST;
}

static bool test_info_task_stats_fails(void) {
    BEGIN_TEST;
    // Spin up a thread.
    mxr_thread_t thread;
    mx_handle_t thandle;
    ASSERT_TRUE(start_thread(threads_test_sleep_fn, (void*)mx_deadline_after(MX_MSEC(100)), &thread,
                             &thandle), "");
    ASSERT_EQ(mx_object_wait_one(thandle,
                                 MX_THREAD_TERMINATED, MX_TIME_INFINITE, NULL),
              MX_OK, "");

    // Ensure that task_stats doesn't work on it.
    mx_info_task_stats_t info;
    EXPECT_NE(mx_object_get_info(thandle, MX_INFO_TASK_STATS,
                                 &info, sizeof(info), NULL, NULL),
              MX_OK,
              "Just added thread support to info_task_status?");
    // If so, replace this with a real test; see example in process.cpp.

    ASSERT_EQ(mx_handle_close(thandle), MX_OK, "");
    END_TEST;
}

static bool test_resume_suspended(void) {
    BEGIN_TEST;

    mx_handle_t event;
    mxr_thread_t thread;
    mx_handle_t thread_h;

    ASSERT_EQ(mx_event_create(0, &event), MX_OK, "");
    ASSERT_TRUE(start_thread(threads_test_wait_fn, &event, &thread, &thread_h), "");
    ASSERT_EQ(mx_task_suspend(thread_h), MX_OK, "");
    ASSERT_EQ(mx_task_resume(thread_h, 0), MX_OK, "");

    // The thread should still be blocked on the event when it wakes up
    ASSERT_EQ(mx_object_wait_one(thread_h, MX_THREAD_TERMINATED, mx_deadline_after(MX_MSEC(100)),
                                 NULL), MX_ERR_TIMED_OUT, "");

    // Verify thread is blocked
    mx_info_thread_t info;
    ASSERT_EQ(mx_object_get_info(thread_h, MX_INFO_THREAD,
                                 &info, sizeof(info), NULL, NULL),
              MX_OK, "");
    ASSERT_EQ(info.wait_exception_port_type, MX_EXCEPTION_PORT_TYPE_NONE, "");
    ASSERT_EQ(info.state, MX_THREAD_STATE_BLOCKED, "");

    // Attach to debugger port so we can see MX_EXCP_THREAD_SUSPENDED
    mx_handle_t eport;
    ASSERT_TRUE(set_debugger_exception_port(&eport),"");

    // Check that signaling the event while suspended results in the expected
    // behavior
    ASSERT_TRUE(suspend_thread_synchronous(thread_h, eport), "");

    // Verify thread is suspended
    ASSERT_EQ(mx_object_get_info(thread_h, MX_INFO_THREAD,
                                 &info, sizeof(info), NULL, NULL),
              MX_OK, "");
    ASSERT_EQ(info.state, MX_THREAD_STATE_SUSPENDED, "");
    ASSERT_EQ(info.wait_exception_port_type, MX_EXCEPTION_PORT_TYPE_NONE, "");

    // Since the thread is suspended the signaling should not take effect.
    ASSERT_EQ(mx_object_signal(event, 0, MX_USER_SIGNAL_0), MX_OK, "");
    ASSERT_EQ(mx_object_wait_one(event, MX_USER_SIGNAL_1, mx_deadline_after(MX_MSEC(100)), NULL), MX_ERR_TIMED_OUT, "");

    ASSERT_EQ(mx_task_resume(thread_h, 0), MX_OK, "");

    ASSERT_EQ(mx_object_wait_one(event, MX_USER_SIGNAL_1, MX_TIME_INFINITE, NULL), MX_OK, "");
    ASSERT_EQ(mx_object_wait_one(
        thread_h, MX_THREAD_TERMINATED, MX_TIME_INFINITE, NULL), MX_OK, "");

    ASSERT_EQ(mx_handle_close(eport), MX_OK, "");
    ASSERT_EQ(mx_handle_close(event), MX_OK, "");
    ASSERT_EQ(mx_handle_close(thread_h), MX_OK, "");

    END_TEST;
}

static bool test_suspend_sleeping(void) {
    BEGIN_TEST;

    const mx_time_t sleep_deadline = mx_deadline_after(MX_MSEC(100));
    mxr_thread_t thread;

    // TODO(teisenbe): This code could be made less racy with a deadline sleep
    // mode when we get one.
    mx_handle_t thread_h;
    ASSERT_TRUE(start_thread(threads_test_sleep_fn, (void*)sleep_deadline, &thread, &thread_h), "");

    mx_nanosleep(sleep_deadline - MX_MSEC(50));

    // Suspend the thread.  Use the debugger port to wait for the suspension.
    mx_handle_t eport;
    ASSERT_TRUE(set_debugger_exception_port(&eport), "");
    ASSERT_TRUE(suspend_thread_synchronous(thread_h, eport), "");
    ASSERT_EQ(mx_handle_close(eport), MX_OK, "");

    ASSERT_EQ(mx_task_resume(thread_h, 0), MX_OK, "");

    // Wait for the sleep to finish
    ASSERT_EQ(mx_object_wait_one(thread_h, MX_THREAD_TERMINATED, sleep_deadline + MX_MSEC(50), NULL),
              MX_OK, "");
    const mx_time_t now = mx_time_get(MX_CLOCK_MONOTONIC);
    ASSERT_GE(now, sleep_deadline, "thread did not sleep long enough");

    ASSERT_EQ(mx_handle_close(thread_h), MX_OK, "");
    END_TEST;
}

static bool test_suspend_channel_call(void) {
    BEGIN_TEST;

    mxr_thread_t thread;

    mx_handle_t channel;
    struct channel_call_suspend_test_arg thread_arg;
    ASSERT_EQ(mx_channel_create(0, &thread_arg.channel, &channel), MX_OK, "");
    thread_arg.call_status = MX_ERR_BAD_STATE;

    mx_handle_t thread_h;
    ASSERT_TRUE(start_thread(threads_test_channel_call_fn, &thread_arg, &thread, &thread_h), "");

    // Wait for the thread to send a channel call before suspending it
    ASSERT_EQ(mx_object_wait_one(channel, MX_CHANNEL_READABLE, MX_TIME_INFINITE, NULL),
              MX_OK, "");

    // Suspend the thread.  Use the debugger port to wait for the suspension.
    mx_handle_t eport;
    ASSERT_TRUE(set_debugger_exception_port(&eport), "");
    ASSERT_TRUE(suspend_thread_synchronous(thread_h, eport), "");
    ASSERT_EQ(mx_handle_close(eport), MX_OK, "");

    // Read the message
    uint8_t buf[9];
    uint32_t actual_bytes;
    ASSERT_EQ(mx_channel_read(channel, 0, buf, NULL, sizeof(buf), 0, &actual_bytes, NULL),
              MX_OK, "");
    ASSERT_EQ(actual_bytes, sizeof(buf), "");
    ASSERT_EQ(memcmp(buf, "abcdefghi", sizeof(buf)), 0, "");

    // Write a reply
    buf[8] = 'j';
    ASSERT_EQ(mx_channel_write(channel, 0, buf, sizeof(buf), NULL, 0), MX_OK, "");

    // Make sure the remote channel didn't get signaled
    EXPECT_EQ(mx_object_wait_one(thread_arg.channel, MX_CHANNEL_READABLE, 0, NULL),
              MX_ERR_TIMED_OUT, "");

    // Make sure we can't read from the remote channel (the message should have
    // been reserved for the other thread, even though it is suspended).
    EXPECT_EQ(mx_channel_read(thread_arg.channel, 0, buf, NULL, sizeof(buf), 0,
                              &actual_bytes, NULL),
              MX_ERR_SHOULD_WAIT, "");

    // Wake the suspended thread
    ASSERT_EQ(mx_task_resume(thread_h, 0), MX_OK, "");

    // Wait for the thread to finish
    ASSERT_EQ(mx_object_wait_one(thread_h, MX_THREAD_TERMINATED, MX_TIME_INFINITE, NULL),
              MX_OK, "");
    EXPECT_EQ(thread_arg.call_status, MX_OK, "");
    EXPECT_EQ(thread_arg.read_status, MX_OK, "");

    ASSERT_EQ(mx_handle_close(channel), MX_OK, "");
    ASSERT_EQ(mx_handle_close(thread_h), MX_OK, "");

    END_TEST;
}

static bool test_suspend_port_call(void) {
    BEGIN_TEST;

    mxr_thread_t thread;
    mx_handle_t port[2];
    ASSERT_EQ(mx_port_create(0, &port[0]), MX_OK, "");
    ASSERT_EQ(mx_port_create(0, &port[1]), MX_OK, "");

    mx_handle_t thread_h;
    ASSERT_TRUE(start_thread(threads_test_port_fn, port, &thread, &thread_h), "");

    mx_nanosleep(mx_deadline_after(MX_MSEC(100)));
    ASSERT_EQ(mx_task_suspend(thread_h), MX_OK, "");

    mx_port_packet_t packet1 = { 100ull, MX_PKT_TYPE_USER, 0u, {} };
    mx_port_packet_t packet2 = { 300ull, MX_PKT_TYPE_USER, 0u, {} };

    ASSERT_EQ(mx_port_queue(port[0], &packet1, 0u), MX_OK, "");
    ASSERT_EQ(mx_port_queue(port[0], &packet2, 0u), MX_OK, "");

    mx_port_packet_t packet;
    ASSERT_EQ(mx_port_wait(port[1], mx_deadline_after(MX_MSEC(100)), &packet, 0u), MX_ERR_TIMED_OUT, "");

    ASSERT_EQ(mx_task_resume(thread_h, 0), MX_OK, "");

    ASSERT_EQ(mx_port_wait(port[1], MX_TIME_INFINITE, &packet, 0u), MX_OK, "");
    EXPECT_EQ(packet.key, 105ull, "");

    ASSERT_EQ(mx_port_wait(port[0], MX_TIME_INFINITE, &packet, 0u), MX_OK, "");
    EXPECT_EQ(packet.key, 300ull, "");

    ASSERT_EQ(mx_object_wait_one(
        thread_h, MX_THREAD_TERMINATED, MX_TIME_INFINITE, NULL), MX_OK, "");

    ASSERT_EQ(mx_handle_close(thread_h), MX_OK, "");
    ASSERT_EQ(mx_handle_close(port[0]), MX_OK, "");
    ASSERT_EQ(mx_handle_close(port[1]), MX_OK, "");

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

    struct test_writing_thread_arg arg = { .v = 0 };
    mx_handle_t thread_h;
    ASSERT_TRUE(start_thread(test_writing_thread_fn, &arg, &thread, &thread_h), "");

    while (arg.v != 1) {
        mx_nanosleep(0);
    }
    ASSERT_EQ(mx_task_suspend(thread_h), MX_OK, "");
    while (arg.v != 2) {
        arg.v = 2;
        // Give the thread a chance to clobber the value
        mx_nanosleep(mx_deadline_after(MX_MSEC(50)));
    }
    ASSERT_EQ(mx_task_resume(thread_h, 0), MX_OK, "");
    while (arg.v != 1) {
        mx_nanosleep(0);
    }

    // Clean up.
    ASSERT_EQ(mx_task_kill(thread_h), MX_OK, "");
    // Wait for the thread termination to complete.  We should do this so
    // that any later tests which use set_debugger_exception_port() do not
    // receive an MX_EXCP_THREAD_EXITING event.
    ASSERT_EQ(mx_object_wait_one(thread_h, MX_THREAD_TERMINATED,
                                 MX_TIME_INFINITE, NULL), MX_OK, "");
    ASSERT_EQ(mx_handle_close(thread_h), MX_OK, "");

    END_TEST;
}

// This tests for a bug in which killing a suspended thread causes the
// thread to be resumed and execute more instructions in userland.
static bool test_kill_suspended_thread(void) {
    BEGIN_TEST;

    mxr_thread_t thread;
    struct test_writing_thread_arg arg = { .v = 0 };
    mx_handle_t thread_h;
    ASSERT_TRUE(start_thread(test_writing_thread_fn, &arg, &thread, &thread_h), "");

    // Wait until the thread has started and has modified arg.v.
    while (arg.v != 1) {
        mx_nanosleep(0);
    }

    // Attach to debugger port so we can see MX_EXCP_THREAD_SUSPENDED.
    mx_handle_t eport;
    ASSERT_TRUE(set_debugger_exception_port(&eport),"");

    ASSERT_TRUE(suspend_thread_synchronous(thread_h, eport), "");

    // Reset the test memory location.
    arg.v = 100;
    ASSERT_EQ(mx_task_kill(thread_h), MX_OK, "");
    // Wait for the thread termination to complete.
    ASSERT_EQ(mx_object_wait_one(thread_h, MX_THREAD_TERMINATED,
                                 MX_TIME_INFINITE, NULL), MX_OK, "");
    // Check for the bug.  The thread should not have resumed execution and
    // so should not have modified arg.v.
    EXPECT_EQ(arg.v, 100, "");

    // Check that the thread is reported as exiting and not as resumed.
    mx_port_packet_t packet;
    ASSERT_EQ(mx_port_wait(eport, MX_TIME_INFINITE, &packet, 0), MX_OK, "");
    ASSERT_EQ(packet.key, kExceptionPortKey, "");
    ASSERT_EQ(packet.type, (uint32_t)MX_EXCP_THREAD_EXITING, "");

    // Clean up.
    ASSERT_EQ(mx_handle_close(eport), MX_OK, "");
    ASSERT_EQ(mx_handle_close(thread_h), MX_OK, "");

    END_TEST;
}

// This tests the registers reported by mx_thread_read_state() for a
// suspended thread.  It starts a thread which sets all the registers to
// known test values.
static bool test_reading_register_state(void) {
    BEGIN_TEST;

    mx_general_regs_t regs_expected;
    regs_fill_test_values(&regs_expected);
    regs_expected.REG_PC = (uintptr_t)spin_with_regs_spin_address;

    mxr_thread_t thread;
    mx_handle_t thread_handle;
    ASSERT_TRUE(start_thread((void (*)(void*))spin_with_regs, &regs_expected,
                             &thread, &thread_handle), "");

    // Allow some time for the thread to begin execution and reach the
    // instruction that spins.
    ASSERT_EQ(mx_nanosleep(mx_deadline_after(MX_MSEC(10))), MX_OK, "");

    // Attach to debugger port so we can see MX_EXCP_THREAD_SUSPENDED.
    mx_handle_t eport;
    ASSERT_TRUE(set_debugger_exception_port(&eport),"");

    ASSERT_TRUE(suspend_thread_synchronous(thread_handle, eport), "");

    mx_general_regs_t regs;
    uint32_t size_read;
    ASSERT_EQ(mx_thread_read_state(thread_handle, MX_THREAD_STATE_REGSET0,
                                   &regs, sizeof(regs), &size_read), MX_OK, "");
    ASSERT_EQ(size_read, sizeof(regs), "");
    ASSERT_TRUE(regs_expect_eq(&regs, &regs_expected), "");

    // Clean up.
    ASSERT_EQ(mx_handle_close(eport), MX_OK, "");
    ASSERT_EQ(mx_task_kill(thread_handle), MX_OK, "");
    // Wait for the thread termination to complete.
    ASSERT_EQ(mx_object_wait_one(thread_handle, MX_THREAD_TERMINATED,
                                 MX_TIME_INFINITE, NULL), MX_OK, "");

    END_TEST;
}

// This tests writing registers using mx_thread_write_state().  After
// setting registers using that syscall, it reads back the registers and
// checks their values.
static bool test_writing_register_state(void) {
    BEGIN_TEST;

    mxr_thread_t thread;
    mx_handle_t thread_handle;
    ASSERT_TRUE(start_thread(threads_test_busy_fn, NULL, &thread,
                             &thread_handle), "");

    // Allow some time for the thread to begin execution and reach the
    // instruction that spins.
    ASSERT_EQ(mx_nanosleep(mx_deadline_after(MX_MSEC(10))), MX_OK, "");

    // Attach to debugger port so we can see MX_EXCP_THREAD_SUSPENDED.
    mx_handle_t eport;
    ASSERT_TRUE(set_debugger_exception_port(&eport),"");

    ASSERT_TRUE(suspend_thread_synchronous(thread_handle, eport), "");

    struct {
        // A small stack that is used for calling mx_thread_exit().
        char stack[1024];
        mx_general_regs_t regs_got;
    } stack;

    mx_general_regs_t regs_to_set;
    regs_fill_test_values(&regs_to_set);
    regs_to_set.REG_PC = (uintptr_t)save_regs_and_exit_thread;
    regs_to_set.REG_STACK_PTR = (uintptr_t)&stack.regs_got;
    ASSERT_EQ(mx_thread_write_state(
                  thread_handle, MX_THREAD_STATE_REGSET0,
                  &regs_to_set, sizeof(regs_to_set)), MX_OK, "");
    ASSERT_EQ(mx_task_resume(thread_handle, 0), MX_OK, "");
    ASSERT_EQ(mx_object_wait_one(thread_handle, MX_THREAD_TERMINATED,
                                 MX_TIME_INFINITE, NULL), MX_OK, "");
    EXPECT_TRUE(regs_expect_eq(&regs_to_set, &stack.regs_got), "");

    // Clean up.
    ASSERT_EQ(mx_handle_close(eport), MX_OK, "");
    ASSERT_EQ(mx_handle_close(thread_handle), MX_OK, "");

    END_TEST;
}

#if defined(__x86_64__)

// This is based on code from kernel/ which isn't usable by code in system/.
enum { X86_CPUID_ADDR_WIDTH = 0x80000008 };

static uint32_t x86_linear_address_width(void) {
    uint32_t eax, ebx, ecx, edx;
    __asm__("cpuid"
            : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
            : "a"(X86_CPUID_ADDR_WIDTH), "c"(0));
    return (eax >> 8) & 0xff;
}

#endif

// Test that mx_thread_write_state() does not allow setting RIP to a
// non-canonical address for a thread that was suspended inside a syscall,
// because if the kernel returns to that address using SYSRET, that can
// cause a fault in kernel mode that is exploitable.  See
// sysret_problem.md.
static bool test_noncanonical_rip_address(void) {
    BEGIN_TEST;

#if defined(__x86_64__)
    mx_handle_t event;
    ASSERT_EQ(mx_event_create(0, &event), MX_OK, "");
    mxr_thread_t thread;
    mx_handle_t thread_handle;
    ASSERT_TRUE(start_thread(threads_test_wait_fn, &event, &thread, &thread_handle), "");

    // Allow some time for the thread to begin execution and block inside
    // the syscall.
    ASSERT_EQ(mx_nanosleep(mx_deadline_after(MX_MSEC(10))), MX_OK, "");

    // Attach to debugger port so we can see MX_EXCP_THREAD_SUSPENDED.
    mx_handle_t eport;
    ASSERT_TRUE(set_debugger_exception_port(&eport),"");

    ASSERT_TRUE(suspend_thread_synchronous(thread_handle, eport), "");

    struct mx_x86_64_general_regs regs;
    uint32_t size_read;
    ASSERT_EQ(mx_thread_read_state(thread_handle, MX_THREAD_STATE_REGSET0,
                                   &regs, sizeof(regs), &size_read), MX_OK, "");
    ASSERT_EQ(size_read, sizeof(regs), "");

    // Example addresses to test.
    uintptr_t noncanonical_addr =
        ((uintptr_t) 1) << (x86_linear_address_width() - 1);
    uintptr_t canonical_addr = noncanonical_addr - 1;
    uint64_t kKernelAddr = 0xffff800000000000;

    struct mx_x86_64_general_regs regs_modified = regs;

    // This RIP address must be disallowed.
    regs_modified.rip = noncanonical_addr;
    ASSERT_EQ(mx_thread_write_state(thread_handle, MX_THREAD_STATE_REGSET0,
                                    &regs_modified, sizeof(regs_modified)),
              MX_ERR_INVALID_ARGS, "");

    regs_modified.rip = canonical_addr;
    ASSERT_EQ(mx_thread_write_state(thread_handle, MX_THREAD_STATE_REGSET0,
                                    &regs_modified, sizeof(regs_modified)),
              MX_OK, "");

    // This RIP address does not need to be disallowed, but it is currently
    // disallowed because this simplifies the check and it's not useful to
    // allow this address.
    regs_modified.rip = kKernelAddr;
    ASSERT_EQ(mx_thread_write_state(thread_handle, MX_THREAD_STATE_REGSET0,
                                    &regs_modified, sizeof(regs_modified)),
              MX_ERR_INVALID_ARGS, "");

    // Clean up: Restore the original register state.
    ASSERT_EQ(mx_thread_write_state(thread_handle, MX_THREAD_STATE_REGSET0,
                                    &regs, sizeof(regs)), MX_OK, "");
    // Allow the child thread to resume and exit.
    ASSERT_EQ(mx_task_resume(thread_handle, 0), MX_OK, "");
    ASSERT_EQ(mx_object_signal(event, 0, MX_USER_SIGNAL_0), MX_OK, "");
    // Wait for the child thread to signal that it has continued.
    ASSERT_EQ(mx_object_wait_one(event, MX_USER_SIGNAL_1, MX_TIME_INFINITE,
                                 NULL), MX_OK, "");
    // Wait for the child thread to exit.
    ASSERT_EQ(mx_object_wait_one(thread_handle, MX_THREAD_TERMINATED, MX_TIME_INFINITE,
                                 NULL), MX_OK, "");
    ASSERT_EQ(mx_handle_close(eport), MX_OK, "");
    ASSERT_EQ(mx_handle_close(event), MX_OK, "");
    ASSERT_EQ(mx_handle_close(thread_handle), MX_OK, "");
#endif

    END_TEST;
}

// Test that, on ARM64, userland cannot use mx_thread_write_state() to
// modify flag bits such as I and F (bits 7 and 6), which are the IRQ and
// FIQ interrupt disable flags.  We don't want userland to be able to set
// those flags to 1, since that would disable interrupts.  Also, userland
// should not be able to read these bits.
static bool test_writing_arm_flags_register(void) {
    BEGIN_TEST;

#if defined(__aarch64__)
    struct test_writing_thread_arg arg = { .v = 0 };
    mxr_thread_t thread;
    mx_handle_t thread_handle;
    ASSERT_TRUE(start_thread(test_writing_thread_fn, &arg, &thread,
                             &thread_handle), "");
    // Wait for the thread to start executing and enter its main loop.
    while (arg.v != 1) {
        ASSERT_EQ(mx_nanosleep(mx_deadline_after(MX_USEC(1))), MX_OK, "");
    }
    // Attach to debugger port so we can see MX_EXCP_THREAD_SUSPENDED.
    mx_handle_t eport;
    ASSERT_TRUE(set_debugger_exception_port(&eport), "");
    ASSERT_TRUE(suspend_thread_synchronous(thread_handle, eport), "");

    mx_general_regs_t regs;
    uint32_t size_read;
    ASSERT_EQ(mx_thread_read_state(thread_handle, MX_THREAD_STATE_REGSET0,
                                   &regs, sizeof(regs), &size_read), MX_OK, "");
    ASSERT_EQ(size_read, sizeof(regs), "");

    // Check that mx_thread_read_state() does not report any more flag bits
    // than are readable via userland instructions.
    const uint64_t kUserVisibleFlags = 0xf0000000;
    EXPECT_EQ(regs.cpsr & ~kUserVisibleFlags, 0u, "");

    // Try setting more flag bits.
    uint64_t original_cpsr = regs.cpsr;
    regs.cpsr |= ~kUserVisibleFlags;
    ASSERT_EQ(mx_thread_write_state(thread_handle, MX_THREAD_STATE_REGSET0,
                                    &regs, sizeof(regs)), MX_OK, "");

    // Firstly, if we read back the register flag, the extra flag bits
    // should have been ignored and should not be reported as set.
    ASSERT_EQ(mx_thread_read_state(thread_handle, MX_THREAD_STATE_REGSET0,
                                   &regs, sizeof(regs), &size_read), MX_OK, "");
    ASSERT_EQ(size_read, sizeof(regs), "");
    EXPECT_EQ(regs.cpsr, original_cpsr, "");

    // Secondly, if we resume the thread, we should be able to kill it.  If
    // mx_thread_write_state() set the interrupt disable flags, then if the
    // thread gets scheduled, it will never get interrupted and we will not
    // be able to kill and join the thread.
    arg.v = 0;
    ASSERT_EQ(mx_task_resume(thread_handle, 0), MX_OK, "");
    // Wait until the thread has actually resumed execution.
    while (arg.v != 1) {
        ASSERT_EQ(mx_nanosleep(mx_deadline_after(MX_USEC(1))), MX_OK, "");
    }
    ASSERT_EQ(mx_task_kill(thread_handle), MX_OK, "");
    ASSERT_EQ(mx_object_wait_one(thread_handle, MX_THREAD_TERMINATED,
                                 MX_TIME_INFINITE, NULL), MX_OK, "");

    // Clean up.
    ASSERT_EQ(mx_handle_close(eport), MX_OK, "");
#endif

    END_TEST;
}

BEGIN_TEST_CASE(threads_tests)
RUN_TEST(test_basics)
RUN_TEST(test_detach)
RUN_TEST(test_long_name_succeeds)
RUN_TEST(test_thread_start_on_initial_thread)
RUN_TEST_ENABLE_CRASH_HANDLER(test_thread_start_with_zero_instruction_pointer)
RUN_TEST(test_kill_busy_thread)
RUN_TEST(test_kill_sleep_thread)
RUN_TEST(test_kill_wait_thread)
RUN_TEST(test_bad_state_nonstarted_thread)
RUN_TEST(test_thread_kills_itself)
RUN_TEST(test_info_task_stats_fails)
RUN_TEST(test_resume_suspended)
RUN_TEST(test_suspend_sleeping)
RUN_TEST(test_suspend_channel_call)
RUN_TEST(test_suspend_port_call)
RUN_TEST(test_suspend_stops_thread)
RUN_TEST(test_kill_suspended_thread)
RUN_TEST(test_reading_register_state)
RUN_TEST(test_writing_register_state)
RUN_TEST(test_noncanonical_rip_address)
RUN_TEST(test_writing_arm_flags_register)
END_TEST_CASE(threads_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
