// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// N.B. We can't test the system exception handler here as that would
// interfere with the global crash logger. A good place to test the
// system exception handler would be in the "core" tests.

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <magenta/compiler.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/exception.h>
#include <magenta/syscalls/port.h>
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

static const char test_child_name[] = "test-child";

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
    tu_channel_write(handle, 0, &data, sizeof(data), &thread, 1);
}

static void send_msg(mx_handle_t handle, enum message msg)
{
    uint64_t data = msg;
    unittest_printf("sending message %d on handle %u\n", msg, handle);
    tu_channel_write(handle, 0, &data, sizeof(data), NULL, 0);
}

static bool recv_msg(mx_handle_t handle, enum message* msg)
{
    uint64_t data;
    uint32_t num_bytes = sizeof(data);

    unittest_printf("waiting for message on handle %u\n", handle);

    if (!tu_wait_readable(handle)) {
        unittest_printf("peer closed while trying to read message\n");
        return false;
    }

    tu_channel_read(handle, 0, &data, &num_bytes, NULL, 0);
    if (num_bytes != sizeof(data)) {
        unittest_printf("recv_msg: unexpected message size, %u != %zu\n",
                        num_bytes, sizeof(data));
        return false;
    }

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
    tu_channel_read(handle, 0, &data, &num_bytes, thread, &num_handles);
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
// resume from a segfault. Such a test is for the debugger API anyway.

static void resume_thread_from_exception(mx_handle_t process, mx_koid_t tid, uint32_t flags)
{
    mx_handle_t thread;
    mx_status_t status = mx_object_get_child(process, tid, MX_RIGHT_SAME_RIGHTS, &thread);
    if (status < 0)
        tu_fatal("mx_object_get_child", status);
    status = mx_task_resume(thread, MX_RESUME_EXCEPTION | flags);
    if (status < 0)
        tu_fatal("mx_mark_exception_handled", status);
}

// Wait for and receive an exception on |eport|.

static bool read_exception(mx_handle_t eport, mx_exception_packet_t* packet)
{
    ASSERT_EQ(mx_port_wait(eport, MX_TIME_INFINITE, packet, sizeof(*packet)), NO_ERROR, "mx_port_wait failed");
    ASSERT_EQ(packet->hdr.key, 0u, "bad report key");
    return true;
}

// TODO(dje): test_not_enough_buffer is wip
// The bool result is because we use the unittest EXPECT/ASSERT macros.

static bool verify_exception(const mx_exception_packet_t* packet,
                             const char* kind,
                             mx_handle_t process,
                             mx_excp_type_t expected_type,
                             bool test_not_enough_buffer,
                             mx_koid_t* tid)
{
    const mx_exception_report_t* report = &packet->report;
    EXPECT_EQ(report->header.type, expected_type, "bad exception type");

    if (strcmp(kind, "process") == 0) {
        // Test mx_object_get_child: Verify it returns the correct process.
        mx_handle_t debug_child;
        mx_status_t status = mx_object_get_child(MX_HANDLE_INVALID, report->context.pid, MX_RIGHT_SAME_RIGHTS, &debug_child);
        if (status < 0)
            tu_fatal("mx_process_debug", status);
        mx_info_handle_basic_t process_info;
        tu_handle_get_basic_info(debug_child, &process_info);
        EXPECT_EQ(process_info.koid, report->context.pid, "mx_process_debug got pid mismatch");
        tu_handle_close(debug_child);
    } else if (strcmp(kind, "thread") == 0) {
        // TODO(dje): Verify exception was from expected thread.
    } else {
        // process/thread gone, nothing to do
    }

    // Verify the exception was from |process|.
    if (process != MX_HANDLE_INVALID) {
        mx_info_handle_basic_t process_info;
        tu_handle_get_basic_info(process, &process_info);
        EXPECT_EQ(process_info.koid, report->context.pid, "wrong process in exception report");
    }

    unittest_printf("%s: exception received: pid %"
                    PRIu64 ", tid %" PRIu64 ", kind %d\n",
                    kind, report->context.pid, report->context.tid,
                    report->header.type);
    *tid = report->context.tid;
    return true;
}

static bool read_and_verify_exception(mx_handle_t eport,
                                      const char* kind,
                                      mx_handle_t process,
                                      mx_excp_type_t expected_type,
                                      bool test_not_enough_buffer,
                                      mx_koid_t* tid)
{
    mx_exception_packet_t packet;
    if (!read_exception(eport, &packet))
        return false;
    return verify_exception(&packet, kind, process, expected_type,
                            test_not_enough_buffer, tid);
}

static void msg_loop(mx_handle_t channel)
{
    bool my_done_tests = false;
    mx_handle_t channel_to_thread = MX_HANDLE_INVALID;

    while (!done_tests && !my_done_tests)
    {
        enum message msg;
        if (!recv_msg(channel, &msg)) {
            unittest_printf("Error while receiving msg\n");
            return;
        }
        switch (msg)
        {
        case MSG_DONE:
            my_done_tests = true;
            break;
        case MSG_CRASH:
            crash_me();
            break;
        case MSG_PING:
            send_msg(channel, MSG_PONG);
            break;
        case MSG_CREATE_AUX_THREAD:
            // Spin up a thread that we can talk to.
            {
                if (channel_to_thread != MX_HANDLE_INVALID) {
                    unittest_printf("previous thread connection not shutdown");
                    return;
                }
                mx_handle_t channel_from_thread;
                tu_channel_create(&channel_to_thread, &channel_from_thread);
                thrd_t thread;
                tu_thread_create_c11(&thread, thread_func, (void*) (uintptr_t) channel_from_thread, "msg-loop-subthread");
                mx_handle_t thread_handle = thrd_get_mx_handle(thread);
                mx_handle_t copy = MX_HANDLE_INVALID;
                mx_handle_duplicate(thread_handle, MX_RIGHT_SAME_RIGHTS, &copy);
                send_msg_new_thread_handle(channel, copy);
            }
            break;
        case MSG_CRASH_AUX_THREAD:
            send_msg(channel_to_thread, MSG_CRASH);
            break;
        case MSG_SHUTDOWN_AUX_THREAD:
            send_msg(channel_to_thread, MSG_DONE);
            mx_handle_close(channel_to_thread);
            channel_to_thread = MX_HANDLE_INVALID;
            break;
        default:
            unittest_printf("unknown message received: %d\n", msg);
            break;
        }
    }
}

static int thread_func(void* arg)
{
    unittest_printf("test thread starting\n");
    mx_handle_t msg_channel = (mx_handle_t) (uintptr_t) arg;
    msg_loop(msg_channel);
    unittest_printf("test thread exiting\n");
    tu_handle_close(msg_channel);
    return 0;
}

static void __NO_RETURN test_child(void)
{
    unittest_printf("Test child starting.\n");
    mx_handle_t channel = mxio_get_startup_handle(MX_HND_TYPE_USER0);
    if (channel == MX_HANDLE_INVALID)
        tu_fatal("mxio_get_startup_handle", ERR_BAD_HANDLE - 1000);
    msg_loop(channel);
    unittest_printf("Test child exiting.\n");
    exit(0);
}

static launchpad_t* setup_test_child(const char* arg, mx_handle_t* out_channel)
{
    if (arg)
        unittest_printf("Starting test child %s.\n", arg);
    else
        unittest_printf("Starting test child.\n");
    mx_handle_t our_channel, their_channel;
    tu_channel_create(&our_channel, &their_channel);
    const char* test_child_path = program_path;
    const char verbosity_string[] = { 'v', '=', utest_verbosity_level + '0', '\0' };
    const char* const argv[] = {
        test_child_path,
        test_child_name,
        verbosity_string,
        arg,
    };
    int argc = countof(argv) - (arg == NULL);
    mx_handle_t handles[1] = { their_channel };
    uint32_t handle_ids[1] = { MX_HND_TYPE_USER0 };
    *out_channel = our_channel;
    launchpad_t* lp = tu_launch_mxio_init(test_child_name, argc, argv, NULL, 1, handles, handle_ids);
    unittest_printf("Test child setup.\n");
    return lp;
}

static void start_test_child(const char* arg,
                             mx_handle_t* out_child, mx_handle_t* out_channel)
{
    launchpad_t* lp = setup_test_child(arg, out_channel);
    *out_child = tu_launch_mxio_fini(lp);
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
    mx_handle_t our_channel, their_channel;
    tu_channel_create(&our_channel, &their_channel);
    thrd_t thread;
    tu_thread_create_c11(&thread, thread_func, (void*) (uintptr_t) their_channel, "thread-set-close-set");
    mx_handle_t thread_handle = thrd_get_mx_handle(thread);
    test_set_close_set("thread", thread_handle);
    send_msg(our_channel, MSG_DONE);
    // thrd_join doesn't provide a timeout, but we have the watchdog for that.
    thrd_join(thread, NULL);
    END_TEST;
}

static void finish_basic_test(const char* kind, mx_handle_t child,
                              mx_handle_t eport, mx_handle_t our_channel,
                              enum message crash_msg)
{
    send_msg(our_channel, crash_msg);
    mx_koid_t tid;
    read_and_verify_exception(eport, kind, child, MX_EXCP_FATAL_PAGE_FAULT, false, &tid);
    resume_thread_from_exception(child, tid, MX_RESUME_NOT_HANDLED);
    tu_wait_signaled(child);

    tu_handle_close(child);
    tu_handle_close(eport);
    tu_handle_close(our_channel);
}

static bool process_handler_test(void)
{
    BEGIN_TEST;
    unittest_printf("process exception handler basic test\n");

    mx_handle_t child, our_channel;
    start_test_child(NULL, &child, &our_channel);
    mx_handle_t eport = tu_io_port_create(0);
    tu_set_exception_port(child, eport, 0, 0);

    finish_basic_test("process", child, eport, our_channel, MSG_CRASH);
    END_TEST;
}

static bool thread_handler_test(void)
{
    BEGIN_TEST;
    unittest_printf("thread exception handler basic test\n");

    mx_handle_t child, our_channel;
    start_test_child(NULL, &child, &our_channel);
    mx_handle_t eport = tu_io_port_create(0);
    send_msg(our_channel, MSG_CREATE_AUX_THREAD);
    mx_handle_t thread;
    recv_msg_new_thread_handle(our_channel, &thread);
    tu_set_exception_port(thread, eport, 0, 0);

    finish_basic_test("thread", child, eport, our_channel, MSG_CRASH_AUX_THREAD);

    tu_handle_close(thread);
    END_TEST;
}

static bool debugger_handler_test(void)
{
    BEGIN_TEST;
    unittest_printf("debugger exception handler basic test\n");

    mx_handle_t child, our_channel;
    start_test_child(NULL, &child, &our_channel);
    mx_handle_t eport = tu_io_port_create(0);
    tu_set_exception_port(child, eport, 0, MX_EXCEPTION_PORT_DEBUGGER);

    finish_basic_test("debugger", child, eport, our_channel, MSG_CRASH);
    END_TEST;
}

static bool process_start_test(void)
{
    BEGIN_TEST;
    unittest_printf("process start test\n");

    mx_handle_t child, our_channel;
    launchpad_t* lp = setup_test_child(NULL, &our_channel);
    mx_handle_t eport = tu_io_port_create(0);
    // Note: child is a borrowed handle, launchpad still owns it at this point.
    child = launchpad_get_process_handle(lp);
    tu_set_exception_port(child, eport, 0, MX_EXCEPTION_PORT_DEBUGGER);
    child = tu_launch_mxio_fini(lp);
    // Now we own the child handle, and lp is destroyed.

    mx_koid_t tid;
    read_and_verify_exception(eport, "process start", child, MX_EXCP_START, false, &tid);
    send_msg(our_channel, MSG_DONE);
    resume_thread_from_exception(child, tid, 0);
    tu_wait_signaled(child);

    tu_handle_close(child);
    tu_handle_close(eport);
    tu_handle_close(our_channel);

    END_TEST;
}

static bool process_gone_notification_test(void)
{
    BEGIN_TEST;
    unittest_printf("process gone notification test\n");

    mx_handle_t child, our_channel;
    start_test_child(NULL, &child, &our_channel);

    mx_handle_t eport = tu_io_port_create(0);
    tu_set_exception_port(child, eport, 0, 0);

    send_msg(our_channel, MSG_DONE);
    mx_koid_t tid;
    read_and_verify_exception(eport, "process gone", child, MX_EXCP_GONE, true, &tid);
    ASSERT_EQ(tid, 0u, "tid not zero");
    // there's no reply to a "gone" notification

    tu_wait_signaled(child);
    tu_handle_close(child);

    tu_handle_close(eport);
    tu_handle_close(our_channel);

    END_TEST;
}

static bool thread_gone_notification_test(void)
{
    BEGIN_TEST;
    unittest_printf("thread gone notification test\n");

    mx_handle_t our_channel, their_channel;
    tu_channel_create(&our_channel, &their_channel);
    mx_handle_t eport = tu_io_port_create(0);
    thrd_t thread;
    tu_thread_create_c11(&thread, thread_func, (void*) (uintptr_t) their_channel, "thread-gone-test-thread");
    mx_handle_t thread_handle = thrd_get_mx_handle(thread);
    tu_set_exception_port(thread_handle, eport, 0, 0);

    send_msg(our_channel, MSG_DONE);
    // TODO(dje): The passing of "self" here is wip.
    mx_koid_t tid;
    read_and_verify_exception(eport, "thread gone", MX_HANDLE_INVALID /*self*/, MX_EXCP_GONE, true, &tid);
    ASSERT_GT(tid, 0u, "tid not >= 0");
    // there's no reply to a "gone" notification

    // thrd_join doesn't provide a timeout, but we have the watchdog for that.
    thrd_join(thread, NULL);

    tu_handle_close(eport);
    tu_handle_close(our_channel);

    END_TEST;
}

static void __NO_RETURN trigger_unsupported(void)
{
    unittest_printf("unsupported exception\n");
    // An unsupported exception is not a failure.
    // Generally it just means that support for the exception doesn't
    // exist yet on this particular architecture.
    exit(0);
}

static void __NO_RETURN trigger_general(void)
{
#if defined(__x86_64__)
#elif defined(__aarch64__)
#endif
    trigger_unsupported();
}

static void __NO_RETURN trigger_fatal_page_fault(void)
{
    *(volatile int*) 0 = 42;
    trigger_unsupported();
}

static void __NO_RETURN trigger_undefined_insn(void)
{
#if defined(__x86_64__)
    __asm__("ud2");
#elif defined(__aarch64__)
    // An instruction not supported at this privilege level will do.
    // ARM calls these "unallocated instructions". Geez, "unallocated"?
    __asm__("mrs x0, elr_el1");
#endif
    trigger_unsupported();
}

static void __NO_RETURN trigger_sw_bkpt(void)
{
#if defined(__x86_64__)
    __asm__("int3");
#elif defined(__aarch64__)
    __asm__("brk 0");
#endif
    trigger_unsupported();
}

static void __NO_RETURN trigger_hw_bkpt(void)
{
#if defined(__x86_64__)
    // We can't set the debug regs from user space, support for setting the
    // debug regs via the debugger interface is work-in-progress, and we can't
    // use "int $1" here. So testing this will have to wait.
#elif defined(__aarch64__)
#endif
    trigger_unsupported();
}

static const struct {
    mx_excp_type_t type;
    const char* name;
    void __NO_RETURN (*trigger_function) (void);
} exceptions[] = {
    { MX_EXCP_GENERAL, "general", trigger_general },
    { MX_EXCP_FATAL_PAGE_FAULT, "page-fault", trigger_fatal_page_fault },
    { MX_EXCP_UNDEFINED_INSTRUCTION, "undefined-insn", trigger_undefined_insn },
    { MX_EXCP_SW_BREAKPOINT, "sw-bkpt", trigger_sw_bkpt },
    { MX_EXCP_HW_BREAKPOINT, "hw-bkpt", trigger_hw_bkpt },
};

static void __NO_RETURN trigger_exception(const char* excp_name)
{
    for (size_t i = 0; i < countof(exceptions); ++i)
    {
        if (strcmp(excp_name, exceptions[i].name) == 0)
        {
            exceptions[i].trigger_function();
        }
    }
    fprintf(stderr, "unknown exception: %s\n", excp_name);
    exit (1);
}

static void __NO_RETURN test_child_trigger(const char* excp_name)
{
    unittest_printf("Exception trigger test child (%s) starting.\n", excp_name);
    trigger_exception(excp_name);
    /* NOTREACHED */
}

static bool trigger_test(void)
{
    BEGIN_TEST;
    unittest_printf("exception trigger tests\n");

    for (size_t i = 0; i < countof(exceptions); ++i) {
        mx_excp_type_t excp_type = exceptions[i].type;
        const char *excp_name = exceptions[i].name;
        mx_handle_t child, our_channel;
        char* arg;
        arg = tu_asprintf("trigger=%s", excp_name);
        launchpad_t* lp = setup_test_child(arg, &our_channel);
        free(arg);
        mx_handle_t eport = tu_io_port_create(0);
        // Note: child is a borrowed handle, launchpad still owns it at this point.
        child = launchpad_get_process_handle(lp);
        tu_set_exception_port(child, eport, 0, MX_EXCEPTION_PORT_DEBUGGER);
        child = tu_launch_mxio_fini(lp);
        // Now we own the child handle, and lp is destroyed.
        mx_koid_t tid;
        read_and_verify_exception(eport, "process start", child, MX_EXCP_START, false, &tid);
        resume_thread_from_exception(child, tid, 0);
        mx_exception_packet_t packet;
        if (read_exception(eport, &packet)) {
            if (packet.report.header.type != MX_EXCP_GONE) {
                verify_exception(&packet, excp_name, child, excp_type, false, &tid);
                resume_thread_from_exception(child, tid, MX_RESUME_NOT_HANDLED);
            }
        }
        tu_wait_signaled(child);
        tu_handle_close(child);
        tu_handle_close(eport);
        tu_handle_close(our_channel);
    }

    END_TEST;
}

BEGIN_TEST_CASE(exceptions_tests)
RUN_TEST(process_set_close_set_test);
RUN_TEST(thread_set_close_set_test);
RUN_TEST(process_handler_test);
RUN_TEST(thread_handler_test);
RUN_TEST(process_start_test);
RUN_TEST(process_gone_notification_test);
RUN_TEST(thread_gone_notification_test);
RUN_TEST(trigger_test);
END_TEST_CASE(exceptions_tests)

static void check_verbosity(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "v=", 2) == 0) {
            int verbosity = atoi(argv[i] + 2);
            unittest_set_verbosity_level(verbosity);
            break;
        }
    }
}

static const char* check_trigger(int argc, char** argv)
{
    static const char trigger[] = "trigger=";
    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], trigger, sizeof(trigger) - 1) == 0) {
            return argv[i] + sizeof(trigger) - 1;
        }
    }
    return NULL;
}

int main(int argc, char **argv)
{
    program_path = argv[0];

    if (argc >= 2 && strcmp(argv[1], test_child_name) == 0) {
        check_verbosity(argc, argv);
        const char* excp_name = check_trigger(argc, argv);
        if (excp_name)
            test_child_trigger(excp_name);
        else
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
