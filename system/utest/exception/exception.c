// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// N.B. We can't fully test the system exception handler here as that would
// interfere with the global crash logger.
// TODO(dbort): A good place to test the system exception handler would be in
// the "core" tests.

#include <assert.h>
#include <inttypes.h>
#include <stdatomic.h>
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

// Setting to true when done turns off the watchdog timer.  This
// must be an atomic so that the compiler does not assume anything
// about when it can be touched.  Otherwise, since the compiler
// knows that vDSO calls don't make direct callbacks, it assumes
// that nothing can happen inside the watchdog loop that would touch
// this variable.  In fact, it will be touched in parallel by
// another thread.
static volatile atomic_bool done_tests;

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

    if (!tu_channel_wait_readable(handle)) {
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

    ASSERT_TRUE(tu_channel_wait_readable(handle), "peer closed while trying to read message");

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

// TODO(dje): test_not_enough_buffer is wip. Remove the argument and
// have a separate explicit test for it.
// TODO(dje): "kind" is losing its value. Delete?
// The bool result is because we use the unittest EXPECT/ASSERT macros.

static bool verify_exception(const mx_exception_packet_t* packet,
                             const char* kind,
                             mx_handle_t process,
                             mx_excp_type_t expected_type,
                             bool test_not_enough_buffer,
                             mx_koid_t* tid)
{
    const mx_exception_report_t* report = &packet->report;

    unittest_printf("%s: exception received: pid %"
                    PRIu64 ", tid %" PRIu64 ", kind %d\n",
                    kind, report->context.pid, report->context.tid,
                    report->header.type);

    EXPECT_EQ(report->header.type, expected_type, "unexpected exception type");

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
        // TODO(dje): Not true. Can move, e.g., tests that verify tid == 0 for
        // "process gone" reports here.
    }

    // Verify the exception was from |process|.
    if (process != MX_HANDLE_INVALID) {
        mx_info_handle_basic_t process_info;
        tu_handle_get_basic_info(process, &process_info);
        EXPECT_EQ(process_info.koid, report->context.pid, "wrong process in exception report");
    }

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

// Wait for a process to exit, and while it's exiting verify we get the
// expected exception reports.
// We may receive thread-exit reports while the process is terminating but
// any other kind of exception is an error.
// This may be used when attached to the process or debugger exception port.
// The bool result is because we use the unittest EXPECT/ASSERT macros.

static bool wait_process_exit(mx_handle_t eport, mx_handle_t process) {
    mx_exception_packet_t packet;
    mx_koid_t tid;

    for (;;) {
        if (!read_exception(eport, &packet))
            return false;
        // If we get a process gone report then all threads have exited.
        if (packet.report.header.type == MX_EXCP_GONE)
            break;
        if (!verify_exception(&packet, "thread-exit", process, MX_EXCP_THREAD_EXIT,
                              false, &tid))
            return false;
        // MX_EXCP_THREAD_EXIT reports must be responded to.
        resume_thread_from_exception(process, tid, 0);
    }

    verify_exception(&packet, "process-gone", process, MX_EXCP_GONE, false, &tid);
    EXPECT_EQ(tid, 0u, "non-zero tid in process gone report");
    // There is no reply to a "process gone" notification.

    // The MX_TASK_TERMINATED signal comes last.
    tu_process_wait_signaled(process);
    return true;
}

// Wait for a process to exit, and while it's exiting verify we get the
// expected exception reports.
// N.B. This is only for use when attached to the debugger exception port:
// only it gets thread-exit reports.
// A thread-exit report for |tid| is expected to be seen.
// We may get other thread-exit reports, that's ok, we don't assume the child
// is single-threaded. But it is an error to get any other kind of exception
// report from a thread.
// The bool result is because we use the unittest EXPECT/ASSERT macros.

static bool wait_process_exit_from_debugger(mx_handle_t eport, mx_handle_t process, mx_koid_t tid) {
    bool tid_seen = false;
    mx_exception_packet_t packet;
    mx_koid_t tid2;

    ASSERT_NEQ(tid, MX_KOID_INVALID, "invalid koid");

    for (;;) {
        if (!read_exception(eport, &packet))
            return false;
        // If we get a process gone report then all threads have exited.
        if (packet.report.header.type == MX_EXCP_GONE)
            break;
        if (!verify_exception(&packet, "thread-exit", process, MX_EXCP_THREAD_EXIT,
                              false, &tid2))
            return false;
        if (tid2 == tid)
            tid_seen = true;
        // MX_EXCP_THREAD_EXIT reports must be responded to.
        resume_thread_from_exception(process, tid2, 0);
    }

    EXPECT_TRUE(tid_seen, "missing MX_EXCP_THREAD_EXIT report");

    verify_exception(&packet, "process-gone", process, MX_EXCP_GONE, false, &tid2);
    EXPECT_EQ(tid2, 0u, "non-zero tid in process gone report");
    // There is no reply to a "process gone" notification.

    // The MX_TASK_TERMINATED signal comes last.
    tu_process_wait_signaled(process);
    return true;
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
        if (atomic_load(&done_tests))
            return 0;
    }
    unittest_printf("WATCHDOG TIMER FIRED\n");
    // This should *cleanly* kill the entire process, not just this thread.
    exit(5);
}

// Tests binding and unbinding behavior.
// |object| must be a valid process or thread handle.
// |debugger| must only be set if |object| is a process handle. If set,
// tests the behavior of binding the debugger eport; otherwise, binds
// the non-debugger exception port.
// This returns "bool" because it uses ASSERT_*.
static bool test_set_close_set(const char* kind, mx_handle_t object,
                               bool debugger) {
    unittest_printf("%s exception handler set-close-set test\n", kind);
    ASSERT_GT(object, 0, "invalid handle");
    uint32_t options = debugger ? MX_EXCEPTION_PORT_DEBUGGER : 0;

    // Bind an exception port to the object.
    mx_handle_t eport = tu_io_port_create(0);
    mx_status_t status;
    status = mx_object_bind_exception_port(object, eport, 0, options);
    ASSERT_EQ(status, NO_ERROR, "error setting exception port");

    // Try binding another exception port to the same object, which should fail.
    mx_handle_t eport2 = tu_io_port_create(0);
    status = mx_object_bind_exception_port(object, eport, 0, options);
    ASSERT_NEQ(status, NO_ERROR, "setting exception port errantly succeeded");

    // Close the ports.
    tu_handle_close(eport2);
    tu_handle_close(eport);
#if 1
    // TODO(MG-307): Dropping all handles to a port should unbind it.
    // Until that works, unbind manually.
    status =
        mx_object_bind_exception_port(object, MX_HANDLE_INVALID, 0, options);
    ASSERT_EQ(status, NO_ERROR, "error resetting exception port");
#endif

    // Verify the close removed the previous handler by successfully
    // adding a new one.
    eport = tu_io_port_create(0);
    status = mx_object_bind_exception_port(object, eport, 0, options);
    ASSERT_EQ(status, NO_ERROR, "error setting exception port (#2)");
    tu_handle_close(eport);
#if 1
    // TODO(MG-307): Dropping all handles to a port should unbind it.
    // Until that works, unbind manually.
    status =
        mx_object_bind_exception_port(object, MX_HANDLE_INVALID, 0, options);
    ASSERT_EQ(status, NO_ERROR, "error resetting exception port (#2)");
#endif

    // Try unbinding from an object without a bound port, which should fail.
    status =
        mx_object_bind_exception_port(object, MX_HANDLE_INVALID, 0, options);
    ASSERT_NEQ(status, NO_ERROR,
               "resetting unbound exception port errantly succeeded");

    return true;
}

static bool process_set_close_set_test(void)
{
    BEGIN_TEST;
    test_set_close_set("process", mx_process_self(), /* debugger */ false);
    END_TEST;
}

static bool process_debugger_set_close_set_test(void)
{
    BEGIN_TEST;
    test_set_close_set("process-debugger",
                       mx_process_self(), /* debugger */ true);
    END_TEST;
}

static bool thread_set_close_set_test(void)
{
    BEGIN_TEST;
    mx_handle_t our_channel, their_channel;
    tu_channel_create(&our_channel, &their_channel);
    thrd_t thread;
    tu_thread_create_c11(&thread, thread_func, (void*)(uintptr_t)their_channel,
                         "thread-set-close-set");
    mx_handle_t thread_handle = thrd_get_mx_handle(thread);
    test_set_close_set("thread", thread_handle, /* debugger */ false);
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
    resume_thread_from_exception(child, tid, MX_RESUME_TRY_NEXT);
    tu_process_wait_signaled(child);

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

    wait_process_exit_from_debugger(eport, child, tid);

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

    wait_process_exit(eport, child);

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
    // Attach to the thread exception report as we're testing for MX_EXCP_GONE
    // reports from the thread here.
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
            if (packet.report.header.type != MX_EXCP_THREAD_EXIT) {
                verify_exception(&packet, excp_name, child, excp_type, false, &tid);
                resume_thread_from_exception(child, tid, MX_RESUME_TRY_NEXT);
                mx_koid_t tid2;
                read_and_verify_exception(eport, "thread exit", child, MX_EXCP_THREAD_EXIT, false, &tid2);
                ASSERT_EQ(tid2, tid, "exiting tid mismatch");
            }
            resume_thread_from_exception(child, tid, 0);

            // We've already seen tid's thread-exit report, so just skip that
            // test here.
            wait_process_exit(eport, child);
        }

        tu_handle_close(child);
        tu_handle_close(eport);
        tu_handle_close(our_channel);
    }

    END_TEST;
}

static bool unbind_while_stopped_test(void)
{
    BEGIN_TEST;
    unittest_printf("unbind_while_stopped tests\n");

    mx_handle_t child, our_channel;
    const char* arg = "";
    launchpad_t* lp = setup_test_child(arg, &our_channel);
    mx_handle_t eport = tu_io_port_create(0);
    // Note: child is a borrowed handle, launchpad still owns it at this point.
    child = launchpad_get_process_handle(lp);
    tu_set_exception_port(child, eport, 0, MX_EXCEPTION_PORT_DEBUGGER);
    child = tu_launch_mxio_fini(lp);
    // Now we own the child handle, and lp is destroyed.
    mx_koid_t tid;
    read_and_verify_exception(eport, "process start", child, MX_EXCP_START, false, &tid);

    // Now unbind the exception port and wait for the child to cleanly exit.
    // If this doesn't work the thread will stay blocked, we'll timeout, and
    // the watchdog will trigger.
    tu_set_exception_port(child, MX_HANDLE_INVALID, 0, MX_EXCEPTION_PORT_DEBUGGER);
    send_msg(our_channel, MSG_DONE);
    tu_process_wait_signaled(child);

    tu_handle_close(child);
    tu_handle_close(eport);
    tu_handle_close(our_channel);

    END_TEST;
}

static bool unbind_rebind_while_stopped_test(void)
{
    BEGIN_TEST;
    unittest_printf("unbind_rebind_while_stopped tests\n");

    mx_handle_t child, our_channel;
    const char* arg = "";
    launchpad_t* lp = setup_test_child(arg, &our_channel);
    mx_handle_t eport = tu_io_port_create(0);
    // Note: child is a borrowed handle, launchpad still owns it at this point.
    child = launchpad_get_process_handle(lp);
    tu_set_exception_port(child, eport, 0, MX_EXCEPTION_PORT_DEBUGGER);
    child = tu_launch_mxio_fini(lp);
    // Now we own the child handle, and lp is destroyed.

    mx_koid_t tid;
    mx_exception_packet_t start_packet;
    // Assert reading the start packet succeeds because otherwise the rest
    // of the test is moot.
    ASSERT_TRUE(read_exception(eport, &start_packet), "error reading start exception");
    ASSERT_TRUE(verify_exception(&start_packet, "process start", child,
                                 MX_EXCP_START, false, &tid),
                "unexpected exception");

    // Unbind the exception port quietly, meaning to leave the thread
    // waiting for an exception response.
    tu_set_exception_port(child, MX_HANDLE_INVALID, 0,
                          MX_EXCEPTION_PORT_DEBUGGER | MX_EXCEPTION_PORT_UNBIND_QUIETLY);

    // Rebind and fetch the exception report, it should match the one
    // we just got.

    tu_set_exception_port(child, eport, 0, MX_EXCEPTION_PORT_DEBUGGER);
    mx_handle_t thread;
    mx_status_t status = mx_object_get_child(child, tid, MX_RIGHT_SAME_RIGHTS, &thread);
    if (status < 0)
        tu_fatal("mx_object_get_child", status);

    // Verify mx_info_thread_t indicates waiting for debugger response.
    mx_info_thread_t info;
    status = mx_object_get_info(thread, MX_INFO_THREAD, &info, sizeof(info), NULL, NULL);
    if (status < 0)
        tu_fatal("mx_object_get_info(MX_INFO_THREAD)", status);
    EXPECT_EQ(info.wait_exception_port_type, MX_EXCEPTION_PORT_TYPE_DEBUGGER, "wrong exception port type");

    // Verify exception report matches current exception.
    mx_exception_report_t report;
    status = mx_object_get_info(thread, MX_INFO_THREAD_EXCEPTION_REPORT, &report, sizeof(report), NULL, NULL);
    if (status < 0)
        tu_fatal("mx_object_get_info(MX_INFO_THREAD_EXCEPTION_REPORT)", status);
    EXPECT_EQ(report.header.size, start_packet.report.header.size, "size mismatch");
    EXPECT_EQ(report.header.type, start_packet.report.header.type, "type mismatch");
    EXPECT_EQ(report.context.arch_id, start_packet.report.context.arch_id, "arch_id mismatch");
    EXPECT_EQ(report.context.pid, start_packet.report.context.pid, "pid mismatch");
    EXPECT_EQ(report.context.tid, start_packet.report.context.tid, "tid mismatch");
    // The "thread-start" report is a synthetic exception and doesn't contain
    // any arch info yet, so we can't test report.context.arch.

    // Done verifying we got the same exception, send the child on its way
    // and tell it we're done.
    resume_thread_from_exception(child, tid, 0);
    send_msg(our_channel, MSG_DONE);

    wait_process_exit_from_debugger(eport, child, tid);

    // We should still be able to get info on the thread.
    status = mx_object_get_info(thread, MX_INFO_THREAD, &info, sizeof(info), NULL, NULL);
    if (status < 0)
        tu_fatal("mx_object_get_info(MX_INFO_THREAD)", status);
    EXPECT_EQ(info.wait_exception_port_type, MX_EXCEPTION_PORT_TYPE_NONE, "wrong exception port type at thread exit");

    tu_handle_close(thread);
    tu_handle_close(child);
    tu_handle_close(eport);
    tu_handle_close(our_channel);

    END_TEST;
}

BEGIN_TEST_CASE(exceptions_tests)
RUN_TEST(process_set_close_set_test);
RUN_TEST(process_debugger_set_close_set_test);
RUN_TEST(thread_set_close_set_test);
RUN_TEST(process_handler_test);
RUN_TEST(thread_handler_test);
RUN_TEST(process_start_test);
RUN_TEST(process_gone_notification_test);
RUN_TEST(thread_gone_notification_test);
RUN_TEST(trigger_test);
RUN_TEST(unbind_while_stopped_test);
RUN_TEST(unbind_rebind_while_stopped_test);
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

    atomic_store(&done_tests, true);
    // TODO: Add an alarm as thrd_join doesn't provide a timeout.
    thrd_join(watchdog_thread, NULL);
    return success ? 0 : -1;
}
