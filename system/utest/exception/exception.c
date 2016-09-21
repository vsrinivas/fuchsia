// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// N.B. We can't test the system exception handler here as that would
// interfere with the global crash logger. A good place to test the
// system exception handler would be in the "core" tests.

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <magenta/compiler.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/threads.h>
#include <mxio/util.h>
#include <test-utils/test-utils.h>
#include <unittest/unittest.h>

// 0.5 seconds
#define WATCHDOG_DURATION_TICK MX_MSEC(500)
// 5 seconds
#define WATCHDOG_DURATION_TICKS 10

static int thread_func(void* arg);

// argv[0]
static char* program_path;

static const char test_child_name[] = "exceptions_test_child";

// Setting to true when done turns off the watchdog timer.
static bool done_tests;

enum message {
    MSG_DONE,
    MSG_CRASH,
    MSG_PING,
    MSG_PONG,
    MSG_CREATE_AUX_THREAD,
    MSG_AUX_THREAD_HANDLE,
    MSG_CRASH_AUX_THREAD,
    MSG_SHUTDOWN_AUX_THREAD
};

static void crash_me(void)
{
    unittest_printf("Attempting to crash.");
    volatile int* p = 0;
    *p = 42;
}

static void send_msg_new_thread_handle(mx_handle_t handle, mx_handle_t thread)
{
    // Note: The handle is transferred to the receiver.
    uint64_t data = MSG_AUX_THREAD_HANDLE;
    unittest_printf("sending new thread %d message on handle %u\n", thread, handle);
    tu_message_write(handle, &data, sizeof(data), &thread, 1, 0);
}

static void send_msg(mx_handle_t handle, enum message msg)
{
    uint64_t data = msg;
    unittest_printf("sending message %d on handle %u\n", msg, handle);
    tu_message_write(handle, &data, sizeof(data), NULL, 0, 0);
}

// This returns "bool" because it uses ASSERT_*.

static bool recv_msg(mx_handle_t handle, enum message* msg)
{
    uint64_t data;
    uint32_t num_bytes = sizeof(data);

    unittest_printf("waiting for message on handle %u\n", handle);

    ASSERT_TRUE(tu_wait_readable(handle), "peer closed while trying to read message");

    tu_message_read(handle, &data, &num_bytes, NULL, 0, 0);
    ASSERT_EQ(num_bytes, sizeof(data), "unexpected message size");

    *msg = data;
    unittest_printf("received message %d\n", *msg);
    return true;
}

// This returns "bool" because it uses ASSERT_*.

static bool recv_msg_new_thread_handle(mx_handle_t handle, mx_handle_t* thread)
{
    uint64_t data;
    uint32_t num_bytes = sizeof(data);

    unittest_printf("waiting for message on handle %u\n", handle);

    ASSERT_TRUE(tu_wait_readable(handle), "peer closed while trying to read message");

    uint32_t num_handles = 1;
    tu_message_read(handle, &data, &num_bytes, thread, &num_handles, 0);
    ASSERT_EQ(num_bytes, sizeof(data), "unexpected message size");
    ASSERT_EQ(num_handles, 1u, "expected one returned handle");

    enum message msg = data;
    // TODO(dje): WTF
    ASSERT_EQ((int)msg, (int)MSG_AUX_THREAD_HANDLE, "expected MSG_AUX_THREAD_HANDLE");

    unittest_printf("received thread handle %d\n", *thread);
    return true;
}

// "resume" here means "tell the kernel we're done"
// This test assumes no presence of the "debugger API" and therefore we can't
// do tests like causing a segfault and then resuming from it. Such a test is
// for the debugger API anyway.

static void resume_thread_from_exception(mx_handle_t process, mx_koid_t tid)
{
    mx_handle_t thread = mx_debug_task_get_child(process, tid);
    mx_status_t status = mx_task_resume(thread, MX_RESUME_EXCEPTION | MX_RESUME_NOT_HANDLED);
    if (status < 0)
        tu_fatal("mx_mark_exception_handled", status);
}

// This returns "bool" because it uses ASSERT_*.
// TODO(dje): test_not_enough_buffer is wip
// The bool result is because we use the unittest EXPECT/ASSERT macros.

static bool test_received_exception(mx_handle_t eport,
                                    const char* kind,
                                    mx_handle_t process,
                                    bool test_not_enough_buffer,
                                    mx_koid_t* tid)
{
    mx_exception_packet_t packet;
    ASSERT_EQ(mx_port_wait(eport, &packet, sizeof(packet)), NO_ERROR, "mx_port_wait failed");
    const mx_exception_report_t* report = &packet.report;

    EXPECT_EQ(packet.hdr.key, 0u, "bad report key");

    if (strcmp(kind, "process") == 0) {
        mx_handle_t debug_child = mx_debug_task_get_child(MX_HANDLE_INVALID, report->context.pid);
        if (debug_child < 0)
            tu_fatal("mx_process_debug", debug_child);
        mx_info_handle_basic_t process_info;
        tu_handle_get_basic_info(debug_child, &process_info);
        ASSERT_EQ(process_info.rec.koid, report->context.pid, "mx_process_debug got pid mismatch");
        tu_handle_close(debug_child);
    } else if (strcmp(kind, "thread") == 0) {
        // TODO(dje)
    } else {
        // process/thread done, nothing to do
    }

    // Verify the exception was from |process|.
    if (process != MX_HANDLE_INVALID) {
        mx_info_handle_basic_t process_info;
        tu_handle_get_basic_info(process, &process_info);
        ASSERT_EQ(process_info.rec.koid, report->context.pid, "wrong process in exception report");
    }

    unittest_printf("exception received from %s handler: pid %llu, tid %llu\n",
                    kind, report->context.pid, report->context.tid);
    *tid = report->context.tid;
    return true;
}

// This returns "bool" because it uses ASSERT_*.

static bool msg_loop(mx_handle_t pipe)
{
    bool my_done_tests = false;
    mx_handle_t pipe_to_thread = MX_HANDLE_INVALID;

    while (!done_tests && !my_done_tests)
    {
        enum message msg;
        ASSERT_TRUE(recv_msg(pipe, &msg), "Error while receiving msg");
        switch (msg)
        {
        case MSG_DONE:
            my_done_tests = true;
            break;
        case MSG_CRASH:
            crash_me();
            break;
        case MSG_PING:
            send_msg(pipe, MSG_PONG);
            break;
        case MSG_CREATE_AUX_THREAD:
            // Spin up a thread that we can talk to.
            {
                ASSERT_EQ(pipe_to_thread, MX_HANDLE_INVALID, "previous thread connection not shutdown");
                mx_handle_t pipe_from_thread;
                tu_message_pipe_create(&pipe_to_thread, &pipe_from_thread);
                thrd_t thread;
                tu_thread_create_c11(&thread, thread_func, (void*) (uintptr_t) pipe_from_thread, "msg-loop-subthread");
                mx_handle_t thread_handle = thrd_get_mx_handle(thread);
                mx_handle_t copy = mx_handle_duplicate(thread_handle, MX_RIGHT_SAME_RIGHTS);
                send_msg_new_thread_handle(pipe, copy);
            }
            break;
        case MSG_CRASH_AUX_THREAD:
            send_msg(pipe_to_thread, MSG_CRASH);
            break;
        case MSG_SHUTDOWN_AUX_THREAD:
            send_msg(pipe_to_thread, MSG_DONE);
            mx_handle_close(pipe_to_thread);
            pipe_to_thread = MX_HANDLE_INVALID;
            break;
        default:
            unittest_printf("unknown message received: %d\n", msg);
            break;
        }
    }
    return true;
}

static int thread_func(void* arg)
{
    unittest_printf("test thread starting\n");
    mx_handle_t msg_pipe = (mx_handle_t) (uintptr_t) arg;
    msg_loop(msg_pipe);
    unittest_printf("test thread exiting\n");
    tu_handle_close(msg_pipe);
    return 0;
}

static void test_child(void) __NO_RETURN;
static void test_child(void)
{
    unittest_printf("Test child starting.\n");
    mx_handle_t pipe = mxio_get_startup_handle(MX_HND_TYPE_USER0);
    if (pipe == MX_HANDLE_INVALID)
        tu_fatal("mxio_get_startup_handle", ERR_BAD_HANDLE - 1000);
    msg_loop(pipe);
    unittest_printf("Test child exiting.\n");
    exit(0);
}

static void start_test_child(mx_handle_t* out_child, mx_handle_t* out_pipe)
{
    unittest_printf("Starting test child.\n");
    mx_handle_t our_pipe, their_pipe;
    tu_message_pipe_create(&our_pipe, &their_pipe);
    const char* test_child_path = program_path;
    const char* const argv[2] = {
        test_child_path,
        test_child_name
    };
    mx_handle_t handles[1] = { their_pipe };
    uint32_t handle_ids[1] = { MX_HND_TYPE_USER0 };
    mx_handle_t child = tu_launch_mxio_etc(test_child_name, 2, argv, NULL, 1, handles, handle_ids);
    *out_child = child;
    *out_pipe = our_pipe;
    unittest_printf("Test child started.\n");
}

static int watchdog_thread_func(void* arg)
{
    for (int i = 0; i < WATCHDOG_DURATION_TICKS; ++i)
    {
        mx_nanosleep(WATCHDOG_DURATION_TICK);
        if (done_tests)
            return 0;
    }
    unittest_printf("WATCHDOG TIMER FIRED\n");
    // This should *cleanly* kill the entire process, not just this thread.
    exit(5);
}

// This returns "bool" because it uses ASSERT_*.
// |object| = 0 -> test process handler (TODO(dje: for now)
// |object| > 0 -> test thread handler (TODO(dje: for now)

static bool test_set_close_set(const char* kind, mx_handle_t object)
{
    if (object == 0)
        object = mx_process_self();
    unittest_printf("%s exception handler set-close-set test\n", kind);
    mx_handle_t eport = tu_io_port_create(0);
    mx_status_t status;
    if (object < 0)
        status = mx_object_bind_exception_port(0, eport, 0, 0);
    else
        status = mx_object_bind_exception_port(object, eport, 0, 0);
    ASSERT_EQ(status, NO_ERROR, "error setting exception port");
    mx_handle_t eport2 = tu_io_port_create(0);
    if (object < 0)
        status = mx_object_bind_exception_port(0, eport, 0, 0);
    else
        status = mx_object_bind_exception_port(object, eport, 0, 0);
    ASSERT_NEQ(status, NO_ERROR, "setting exception port errantly succeeded");
    tu_handle_close(eport2);
    tu_handle_close(eport);
#if 1 // TODO(dje): wip, close doesn't yet reset the exception port
    if (object < 0)
        status = mx_object_bind_exception_port(0, MX_HANDLE_INVALID, 0, 0);
    else
        status = mx_object_bind_exception_port(object, MX_HANDLE_INVALID, 0, 0);
    ASSERT_EQ(status, NO_ERROR, "error resetting exception port");
#endif
    eport = tu_io_port_create(0);
    // Verify the close removed the previous handler.
    if (object < 0)
        status = mx_object_bind_exception_port(0, eport, 0, 0);
    else
        status = mx_object_bind_exception_port(object, eport, 0, 0);
    ASSERT_EQ(status, NO_ERROR, "error setting exception port (#2)");
    tu_handle_close(eport);
#if 1 // TODO(dje): wip, close doesn't yet reset the exception port
    if (object < 0)
        status = mx_object_bind_exception_port(0, MX_HANDLE_INVALID, 0, 0);
    else
        status = mx_object_bind_exception_port(object, MX_HANDLE_INVALID, 0, 0);
    ASSERT_EQ(status, NO_ERROR, "error resetting exception port");
#endif
    return true;
}

static bool process_set_close_set_test(void)
{
    BEGIN_TEST;
    test_set_close_set("process", 0);
    END_TEST;
}

static bool thread_set_close_set_test(void)
{
    BEGIN_TEST;
    mx_handle_t our_pipe, their_pipe;
    tu_message_pipe_create(&our_pipe, &their_pipe);
    thrd_t thread;
    tu_thread_create_c11(&thread, thread_func, (void*) (uintptr_t) their_pipe, "thread-set-close-set");
    mx_handle_t thread_handle = thrd_get_mx_handle(thread);
    test_set_close_set("thread", thread_handle);
    send_msg(our_pipe, MSG_DONE);
    // thrd_join doesn't provide a timeout, but we have the watchdog for that.
    thrd_join(thread, NULL);
    END_TEST;
}

static void finish_basic_test(const char* kind, mx_handle_t child,
                              mx_handle_t eport, mx_handle_t our_pipe,
                              enum message crash_msg)
{
    send_msg(our_pipe, crash_msg);
    mx_koid_t tid;
    test_received_exception(eport, kind, child, false, &tid);
    resume_thread_from_exception(child, tid);
    tu_wait_signaled(child);

    tu_handle_close(child);
    tu_handle_close(eport);
    tu_handle_close(our_pipe);
}

static bool process_handler_test(void)
{
    BEGIN_TEST;
    unittest_printf("process exception handler basic test\n");

    mx_handle_t child, our_pipe;
    start_test_child(&child, &our_pipe);
    mx_handle_t eport = tu_io_port_create(0);
    tu_set_exception_port(child, eport, 0, 0);

    finish_basic_test("process", child, eport, our_pipe, MSG_CRASH);
    END_TEST;
}

static bool thread_handler_test(void)
{
    BEGIN_TEST;
    unittest_printf("thread exception handler basic test\n");

    mx_handle_t child, our_pipe;
    start_test_child(&child, &our_pipe);
    mx_handle_t eport = tu_io_port_create(0);
    send_msg(our_pipe, MSG_CREATE_AUX_THREAD);
    mx_handle_t thread;
    recv_msg_new_thread_handle(our_pipe, &thread);
    tu_set_exception_port(thread, eport, 0, 0);

    finish_basic_test("thread", child, eport, our_pipe, MSG_CRASH_AUX_THREAD);

    tu_handle_close(thread);
    END_TEST;
}

static bool debugger_handler_test(void)
{
    BEGIN_TEST;
    unittest_printf("debugger exception handler basic test\n");

    mx_handle_t child, our_pipe;
    start_test_child(&child, &our_pipe);
    mx_handle_t eport = tu_io_port_create(0);
    tu_set_exception_port(child, eport, 0, MX_EXCEPTION_PORT_DEBUGGER);

    finish_basic_test("debugger", child, eport, our_pipe, MSG_CRASH);
    END_TEST;
}

static bool process_gone_notification_test(void)
{
    BEGIN_TEST;
    unittest_printf("process gone notification test\n");

    mx_handle_t child, our_pipe;
    start_test_child(&child, &our_pipe);

    mx_handle_t eport = tu_io_port_create(0);
    tu_set_exception_port(child, eport, 0, 0);

    send_msg(our_pipe, MSG_DONE);
    mx_koid_t tid;
    test_received_exception(eport, "process gone", child, true, &tid);
    ASSERT_EQ(tid, 0u, "tid not zero");
    // there's no reply to a "gone" notification

    tu_wait_signaled(child);
    tu_handle_close(child);

    tu_handle_close(eport);
    tu_handle_close(our_pipe);

    END_TEST;
}

static bool thread_gone_notification_test(void)
{
    BEGIN_TEST;
    unittest_printf("thread gone notification test\n");

    mx_handle_t our_pipe, their_pipe;
    tu_message_pipe_create(&our_pipe, &their_pipe);
    mx_handle_t eport = tu_io_port_create(0);
    thrd_t thread;
    tu_thread_create_c11(&thread, thread_func, (void*) (uintptr_t) their_pipe, "thread-gone-test-thread");
    mx_handle_t thread_handle = thrd_get_mx_handle(thread);
    tu_set_exception_port(thread_handle, eport, 0, 0);

    send_msg(our_pipe, MSG_DONE);
    // TODO(dje): The passing of "self" here is wip.
    mx_koid_t tid;
    test_received_exception(eport, "thread gone", MX_HANDLE_INVALID /*self*/, true, &tid);
    ASSERT_GT(tid, 0u, "tid not >= 0");
    // there's no reply to a "gone" notification

    // thrd_join doesn't provide a timeout, but we have the watchdog for that.
    thrd_join(thread, NULL);

    tu_handle_close(eport);
    tu_handle_close(our_pipe);

    END_TEST;
}

BEGIN_TEST_CASE(exceptions_tests)
RUN_TEST(process_set_close_set_test);
RUN_TEST(thread_set_close_set_test);
RUN_TEST(process_handler_test);
RUN_TEST(thread_handler_test);
RUN_TEST(process_gone_notification_test);
RUN_TEST(thread_gone_notification_test);
END_TEST_CASE(exceptions_tests)

int main(int argc, char **argv)
{
    program_path = argv[0];

    if (argc == 2 && strcmp(argv[1], test_child_name) == 0) {
        test_child();
        return 0;
    }

    thrd_t watchdog_thread;
    tu_thread_create_c11(&watchdog_thread, watchdog_thread_func, NULL, "watchdog-thread");

    bool success = unittest_run_all_tests(argc, argv);

    done_tests = true;
    // TODO: Add an alarm as thrd_join doesn't provide a timeout.
    thrd_join(watchdog_thread, NULL);
    return success ? 0 : -1;
}
