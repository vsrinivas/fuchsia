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
#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/port.h>
#include <zircon/threads.h>
#include <test-utils/test-utils.h>
#include <unittest/unittest.h>

static int thread_func(void* arg);

// argv[0]
static char* program_path;

// This is the key that is assigned to the port when bound.
// This value appears in |packet.key| in all exception messages.
static const uint64_t EXCEPTION_PORT_KEY = 0x6b6579; // "key"

static const char test_child_name[] = "test-child";
static const char exit_closing_excp_handle_child_name[] = "exit-closing-excp-handle";

enum message {
    // Make the type of this enum signed so that we don't get a compile failure
    // later with things like EXPECT_EQ(msg, MSG_PONG) [unsigned vs signed
    // comparison mismatch]
    MSG_ENSURE_SIGNED = -1,
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

static void send_msg_new_thread_handle(zx_handle_t handle, zx_handle_t thread)
{
    // Note: The handle is transferred to the receiver.
    uint64_t data = MSG_AUX_THREAD_HANDLE;
    unittest_printf("sending new thread %d message on handle %u\n", thread, handle);
    tu_channel_write(handle, 0, &data, sizeof(data), &thread, 1);
}

static void send_msg(zx_handle_t handle, enum message msg)
{
    uint64_t data = msg;
    unittest_printf("sending message %d on handle %u\n", msg, handle);
    tu_channel_write(handle, 0, &data, sizeof(data), NULL, 0);
}

static bool recv_msg(zx_handle_t handle, enum message* msg)
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

static bool recv_msg_new_thread_handle(zx_handle_t handle, zx_handle_t* thread)
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
    ASSERT_EQ(msg, MSG_AUX_THREAD_HANDLE, "expected MSG_AUX_THREAD_HANDLE");

    unittest_printf("received thread handle %d\n", *thread);
    return true;
}

// "resume" here means "tell the kernel we're done"
// This test assumes no presence of the "debugger API" and therefore we can't
// resume from a segfault. Such a test is for the debugger API anyway.

static void resume_thread_from_exception(zx_handle_t process, zx_koid_t tid,
                                         uint32_t excp_port_type,
                                         uint32_t flags)
{
    zx_handle_t thread;
    zx_status_t status = zx_object_get_child(process, tid, ZX_RIGHT_SAME_RIGHTS, &thread);
    if (status < 0)
        tu_fatal("zx_object_get_child", status);

    zx_info_thread_t info = tu_thread_get_info(thread);
    EXPECT_EQ(info.state, ZX_THREAD_STATE_BLOCKED, "");
    if (excp_port_type != ZX_EXCEPTION_PORT_TYPE_NONE) {
        EXPECT_EQ(info.wait_exception_port_type, excp_port_type, "");
    }

    status = zx_task_resume(thread, ZX_RESUME_EXCEPTION | flags);
    if (status < 0)
        tu_fatal("resume_thread_from_exception", status);
    zx_handle_close(thread);
}

// Wait for and receive an exception or signal on |eport|.

static bool read_packet(zx_handle_t eport, zx_port_packet_t* packet)
{
    ASSERT_EQ(zx_port_wait(eport, ZX_TIME_INFINITE, packet, 1), ZX_OK, "zx_port_wait failed");
    if (ZX_PKT_IS_SIGNAL_REP(packet->type)) {
        unittest_printf("signal received: key %" PRIu64 ", observed 0x%x\n",
                        packet->key, packet->signal.observed);
    } else {
        ASSERT_TRUE(ZX_PKT_IS_EXCEPTION(packet->type), "");
        ASSERT_EQ(packet->key, EXCEPTION_PORT_KEY, "bad report key");
        unittest_printf("exception received: pid %"
                        PRIu64 ", tid %" PRIu64 ", type %d\n",
                        packet->exception.pid, packet->exception.tid, packet->type);
    }
    return true;
}

// The bool result is because we use the unittest EXPECT/ASSERT macros.

static bool verify_exception(const zx_port_packet_t* packet,
                             zx_handle_t process,
                             zx_excp_type_t expected_type)
{
    ASSERT_EQ(packet->type, expected_type, "");
    EXPECT_EQ(packet->key, EXCEPTION_PORT_KEY, "");

    // Verify the exception was from |process|.
    if (process != ZX_HANDLE_INVALID) {
        zx_koid_t pid = tu_get_koid(process);
        EXPECT_EQ(pid, packet->exception.pid, "");
    }

    return true;
}

// The bool result is because we use the unittest EXPECT/ASSERT macros.

static bool verify_signal(const zx_port_packet_t* packet,
                          uint64_t key,
                          zx_signals_t expected_signals)
{
    ASSERT_TRUE(ZX_PKT_IS_SIGNAL_ONE(packet->type) ||
                ZX_PKT_IS_SIGNAL_REP(packet->type),
                "");

    if (key != 0u)
        EXPECT_EQ(packet->key, key, "");
    EXPECT_TRUE(packet->signal.observed & expected_signals, "");

    return true;
}

static bool read_and_verify_exception(zx_handle_t eport,
                                      zx_handle_t process,
                                      zx_excp_type_t expected_type,
                                      zx_koid_t* tid)
{
    zx_port_packet_t packet;
    if (!read_packet(eport, &packet))
        return false;
    *tid = packet.exception.tid;
    return verify_exception(&packet, process, expected_type);
}

// Wait for a process to exit, and while it's exiting verify we get the
// expected exception reports.
// The caller must have attached an async-wait for |process| to |eport|.
// See start_test_child_with_eport().
// We may receive thread-exit reports while the process is terminating but
// any other kind of exception is an error.
// This may be used when attached to the process or debugger exception port.
// The bool result is because we use the unittest EXPECT/ASSERT macros.

static bool wait_process_exit(zx_handle_t eport, zx_handle_t process) {
    zx_port_packet_t packet;
    zx_koid_t pid = tu_get_koid(process);

    for (;;) {
        unittest_printf("%s: calling read_packet\n", __func__);
        if (!read_packet(eport, &packet))
            return false;
        unittest_printf("%s: read_packet done\n", __func__);
        // If we get a process exit signal then all threads have exited.
        // Any other signal packet is an error.
        if (ZX_PKT_IS_SIGNAL_ONE(packet.type) ||
            ZX_PKT_IS_SIGNAL_REP(packet.type)) {
            if (packet.key == pid && (packet.signal.observed & ZX_PROCESS_TERMINATED))
                break;
            ASSERT_TRUE(false, "");
        }
        if (!verify_exception(&packet, process, ZX_EXCP_THREAD_EXITING))
            return false;
        // ZX_EXCP_THREAD_EXITING reports must normally be responded to.
        // However, when the process exits it kills all threads which will
        // kick them out of the ExceptionHandlerExchange. Thus there's no
        // need to resume them here.
    }

    // This isn't necessary, but it tests being able to wait on the process
    // handle directly, after having waited on it via |eport|.
    tu_process_wait_signaled(process);
    return true;
}

// Wait for a process to exit, and while it's exiting verify we get the
// expected exception reports.
// The caller must have attached an async-wait for |process| to |eport|.
// See start_test_child_with_eport().
// N.B. This is only for use when attached to the debugger exception port:
// only it gets thread-exit reports.
// A thread-exit report for |tid| is expected to be seen.
// We may get other thread-exit reports, that's ok, we don't assume the child
// is single-threaded. But it is an error to get any other kind of exception
// report from a thread.
// The bool result is because we use the unittest EXPECT/ASSERT macros.

static bool wait_process_exit_from_debugger(zx_handle_t eport, zx_handle_t process, zx_koid_t tid) {
    bool tid_seen = false;
    zx_port_packet_t packet;
    zx_koid_t pid = tu_get_koid(process);

    ASSERT_NE(tid, ZX_KOID_INVALID, "invalid koid");

    for (;;) {
        unittest_printf("%s: calling read_packet\n", __func__);
        if (!read_packet(eport, &packet))
            return false;
        unittest_printf("%s: read_packet done\n", __func__);
        // If we get a process exit signal then all threads have exited.
        // Any other signal packet is an error.
        if (ZX_PKT_IS_SIGNAL_ONE(packet.type) ||
            ZX_PKT_IS_SIGNAL_REP(packet.type)) {
            if (packet.key == pid && (packet.signal.observed & ZX_PROCESS_TERMINATED))
                break;
            ASSERT_TRUE(false, "");
        }
        if (!verify_exception(&packet, process, ZX_EXCP_THREAD_EXITING))
            return false;
        if (packet.exception.tid == tid)
            tid_seen = true;
        // ZX_EXCP_THREAD_EXITING reports must normally be responded to.
        // However, when the process exits it kills all threads which will
        // kick them out of the ExceptionHandlerExchange. So send this thread
        // on its way, but it's ok if the thread is gone.
        zx_handle_t thread;
        zx_status_t status = zx_object_get_child(process, tid, ZX_RIGHT_SAME_RIGHTS, &thread);
        if (status == ZX_OK) {
            status = zx_task_resume(thread, ZX_RESUME_EXCEPTION);
            if (status < 0) {
                // If the resume failed the thread must be dying or dead.
                EXPECT_EQ(status, ZX_ERR_BAD_STATE, "");
                EXPECT_TRUE(tu_thread_is_dying_or_dead(thread), "");
            }
            zx_handle_close(thread);
        }
    }

    EXPECT_TRUE(tid_seen, "missing ZX_EXCP_THREAD_EXITING report");

    // This isn't necessary, but it tests being able to wait on the process
    // handle directly, after having waited on it via |eport|.
    tu_process_wait_signaled(process);
    return true;
}

static bool ensure_child_running(zx_handle_t channel) {
    // Note: This function is called from external threads and thus does
    // not use EXPECT_*/ASSERT_*.
    enum message msg;
    send_msg(channel, MSG_PING);
    if (!recv_msg(channel, &msg)) {
        unittest_printf("ensure_child_running: Error while receiving msg\n");
        return false;
    }
    if (msg != MSG_PONG) {
        unittest_printf("ensure_child_running: expecting PONG, got %d instead\n", msg);
        return false;
    }
    return true;
}

static void msg_loop(zx_handle_t channel)
{
    bool my_done_tests = false;
    zx_handle_t channel_to_thread = ZX_HANDLE_INVALID;

    while (!my_done_tests)
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
                if (channel_to_thread != ZX_HANDLE_INVALID) {
                    unittest_printf("previous thread connection not shutdown");
                    return;
                }
                zx_handle_t channel_from_thread;
                tu_channel_create(&channel_to_thread, &channel_from_thread);
                thrd_t thread;
                tu_thread_create_c11(&thread, thread_func, (void*) (uintptr_t) channel_from_thread, "msg-loop-subthread");
                // Make sure the new thread is up and running before sending
                // its handle back: this removes potential problems like
                // needing to handle ZX_EXCP_THREAD_STARTING exceptions if the
                // debugger exception port is bound later.
                if (ensure_child_running(channel_to_thread)) {
                    zx_handle_t thread_handle = thrd_get_zx_handle(thread);
                    zx_handle_t copy = tu_handle_duplicate(thread_handle);
                    send_msg_new_thread_handle(channel, copy);
                } else {
                    // We could terminate the thread or some such, but the
                    // process will be killed by our "caller".
                    send_msg_new_thread_handle(channel, ZX_HANDLE_INVALID);
                    zx_handle_close(channel_to_thread);
                    channel_to_thread = ZX_HANDLE_INVALID;
                }
            }
            break;
        case MSG_CRASH_AUX_THREAD:
            send_msg(channel_to_thread, MSG_CRASH);
            break;
        case MSG_SHUTDOWN_AUX_THREAD:
            send_msg(channel_to_thread, MSG_DONE);
            zx_handle_close(channel_to_thread);
            channel_to_thread = ZX_HANDLE_INVALID;
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
    zx_handle_t msg_channel = (zx_handle_t) (uintptr_t) arg;
    msg_loop(msg_channel);
    unittest_printf("test thread exiting\n");
    tu_handle_close(msg_channel);
    return 0;
}

static void __NO_RETURN test_child(void)
{
    unittest_printf("Test child starting.\n");
    zx_handle_t channel = zx_get_startup_handle(PA_USER0);
    if (channel == ZX_HANDLE_INVALID)
        tu_fatal("zx_get_startup_handle", ZX_ERR_BAD_HANDLE - 1000);
    msg_loop(channel);
    unittest_printf("Test child exiting.\n");
    exit(0);
}

static launchpad_t* setup_test_child(zx_handle_t job, const char* arg,
                                     zx_handle_t* out_channel)
{
    unittest_printf("Starting test child %s.\n", arg);
    zx_handle_t our_channel, their_channel;
    tu_channel_create(&our_channel, &their_channel);
    const char* test_child_path = program_path;
    const char verbosity_string[] = { 'v', '=', utest_verbosity_level + '0', '\0' };
    const char* const argv[] = {
        test_child_path,
        arg,
        verbosity_string,
    };
    int argc = countof(argv);
    zx_handle_t handles[1] = { their_channel };
    uint32_t handle_ids[1] = { PA_USER0 };
    *out_channel = our_channel;
    launchpad_t* lp = tu_launch_fdio_init(job, test_child_name, argc, argv,
                                          NULL, 1, handles, handle_ids);
    unittest_printf("Test child setup.\n");
    return lp;
}

static void start_test_child(zx_handle_t job, const char* arg,
                             zx_handle_t* out_child, zx_handle_t* out_channel)
{
    launchpad_t* lp = setup_test_child(job, arg, out_channel);
    *out_child = tu_launch_fdio_fini(lp);
    unittest_printf("Test child started.\n");
}

static void start_test_child_with_eport(zx_handle_t job, const char* arg,
                                        zx_handle_t* out_child,
                                        zx_handle_t* out_eport,
                                        zx_handle_t* out_channel)
{
    launchpad_t* lp = setup_test_child(zx_job_default(), arg, out_channel);
    zx_handle_t eport = tu_io_port_create();
    // Note: child is a borrowed handle, launchpad still owns it at this point.
    zx_handle_t child = launchpad_get_process_handle(lp);
    tu_set_exception_port(child, eport, EXCEPTION_PORT_KEY, ZX_EXCEPTION_PORT_DEBUGGER);
    child = tu_launch_fdio_fini(lp);
    // Now we own the child handle, and lp is destroyed.
    // Note: This is a different handle, the previous child handle is gone at
    // this point (transfered to the child process).
    unittest_printf("child 0x%x, eport 0x%x\n", child, eport);
    tu_object_wait_async(child, eport, ZX_PROCESS_TERMINATED);
    *out_child = child;
    *out_eport = eport;
}

// Tests binding and unbinding behavior.
// |object| must be a valid job, process, or thread handle.
// |debugger| must only be set if |object| is a process handle. If set,
// tests the behavior of binding the debugger eport; otherwise, binds
// the non-debugger exception port.
// This returns "bool" because it uses ASSERT_*.
static bool test_set_close_set(zx_handle_t object, bool debugger) {
    ASSERT_NE(object, ZX_HANDLE_INVALID, "invalid handle");
    uint32_t options = debugger ? ZX_EXCEPTION_PORT_DEBUGGER : 0;

    // Bind an exception port to the object.
    zx_handle_t eport = tu_io_port_create();
    zx_status_t status;
    status = zx_task_bind_exception_port(object, eport, 0, options);
    ASSERT_EQ(status, ZX_OK, "error setting exception port");

    // Try binding another exception port to the same object, which should fail.
    zx_handle_t eport2 = tu_io_port_create();
    status = zx_task_bind_exception_port(object, eport, 0, options);
    ASSERT_NE(status, ZX_OK, "setting exception port errantly succeeded");

    // Close the ports.
    tu_handle_close(eport2);
    tu_handle_close(eport);

    // Verify the close removed the previous handler by successfully
    // adding a new one.
    eport = tu_io_port_create();
    status = zx_task_bind_exception_port(object, eport, 0, options);
    ASSERT_EQ(status, ZX_OK, "error setting exception port (#2)");
    tu_handle_close(eport);

    // Try unbinding from an object without a bound port, which should fail.
    status =
        zx_task_bind_exception_port(object, ZX_HANDLE_INVALID, 0, options);
    ASSERT_NE(status, ZX_OK,
              "resetting unbound exception port errantly succeeded");

    return true;
}

static bool job_set_close_set_test(void)
{
    BEGIN_TEST;
    zx_handle_t job = tu_job_create(zx_job_default());
    test_set_close_set(job, /* debugger */ false);
    tu_handle_close(job);
    END_TEST;
}

static bool process_set_close_set_test(void)
{
    BEGIN_TEST;
    test_set_close_set(zx_process_self(), /* debugger */ false);
    END_TEST;
}

static bool process_debugger_set_close_set_test(void)
{
    BEGIN_TEST;
    test_set_close_set(zx_process_self(), /* debugger */ true);
    END_TEST;
}

static bool thread_set_close_set_test(void)
{
    BEGIN_TEST;
    zx_handle_t our_channel, their_channel;
    tu_channel_create(&our_channel, &their_channel);
    thrd_t thread;
    tu_thread_create_c11(&thread, thread_func, (void*)(uintptr_t)their_channel,
                         "thread-set-close-set");
    zx_handle_t thread_handle = thrd_get_zx_handle(thread);
    test_set_close_set(thread_handle, /* debugger */ false);
    send_msg(our_channel, MSG_DONE);
    // thrd_join doesn't provide a timeout, but we have the watchdog for that.
    thrd_join(thread, NULL);
    END_TEST;
}

typedef struct {
    zx_handle_t proc;
    zx_handle_t vmar;
} proc_handles;

// Creates but does not start a process, returning its handles in |*ph|.
// Returns false if an assertion fails.
static bool create_non_running_process(const char* name, proc_handles* ph) {
    memset(ph, 0, sizeof(*ph));
    zx_status_t status = zx_process_create(
        zx_job_default(), name, strlen(name), 0, &ph->proc, &ph->vmar);
    ASSERT_EQ(status, ZX_OK, "zx_process_create");
    ASSERT_NE(ph->proc, ZX_HANDLE_INVALID, "proc handle");
    return true;
}

// Closes any valid handles in |ph|.
static void close_proc_handles(proc_handles *ph) {
    if (ph->proc > 0) {
        tu_handle_close(ph->proc);
        ph->proc = ZX_HANDLE_INVALID;
    }
    if (ph->vmar > 0) {
        tu_handle_close(ph->vmar);
        ph->vmar = ZX_HANDLE_INVALID;
    }
}

static bool non_running_process_set_close_set_test(void) {
    BEGIN_TEST;

    // Create but do not start a process.
    proc_handles ph;
    ASSERT_TRUE(create_non_running_process(__func__, &ph), "");

    // Make sure binding and unbinding behaves.
    test_set_close_set(ph.proc, /* debugger */ false);

    close_proc_handles(&ph);
    END_TEST;
}

static bool non_running_process_debugger_set_close_set_test(void) {
    BEGIN_TEST;

    // Create but do not start a process.
    proc_handles ph;
    ASSERT_TRUE(create_non_running_process(__func__, &ph), "");

    // Make sure binding and unbinding behaves.
    test_set_close_set(ph.proc, /* debugger */ true);

    close_proc_handles(&ph);
    END_TEST;
}

static bool non_running_thread_set_close_set_test(void) {
    BEGIN_TEST;

    // Create but do not start a process.
    proc_handles ph;
    ASSERT_TRUE(create_non_running_process(__func__, &ph), "");

    // Create but do not start a thread in that process.
    zx_handle_t thread = ZX_HANDLE_INVALID;
    zx_status_t status =
        zx_thread_create(ph.proc, __func__, sizeof(__func__)-1, 0, &thread);
    ASSERT_EQ(status, ZX_OK, "zx_thread_create");
    ASSERT_NE(thread, ZX_HANDLE_INVALID, "thread handle");

    // Make sure binding and unbinding behaves.
    test_set_close_set(thread, /* debugger */ false);

    tu_handle_close(thread);
    close_proc_handles(&ph);
    END_TEST;
}

// Creates a process, possibly binds an eport to it (if |bind_while_alive| is set),
// then tries to unbind the eport, checking for the expected status.
static bool dead_process_unbind_helper(bool debugger, bool bind_while_alive) {
    const uint32_t options = debugger ? ZX_EXCEPTION_PORT_DEBUGGER : 0;

    // Start a new process.
    zx_handle_t child, our_channel;
    start_test_child(zx_job_default(), test_child_name, &child, &our_channel);

    // Possibly bind an eport to it.
    zx_handle_t eport = ZX_HANDLE_INVALID;
    if (bind_while_alive) {
        // If we're binding to the debugger exception port make sure the
        // child is running first so that we don't have to process
        // ZX_EXCP_THREAD_STARTING.
        if (debugger) {
            ASSERT_TRUE(ensure_child_running(our_channel), "");
        }
        eport = tu_io_port_create();
        tu_set_exception_port(child, eport, EXCEPTION_PORT_KEY, options);
        tu_object_wait_async(child, eport, ZX_PROCESS_TERMINATED);
    }

    // Tell the process to exit and wait for it.
    send_msg(our_channel, MSG_DONE);
    if (debugger && bind_while_alive) {
        // If we bound a debugger port, the process won't die until we
        // consume the exception reports.
        ASSERT_TRUE(wait_process_exit(eport, child), "");
    } else {
        ASSERT_EQ(tu_process_wait_exit(child), 0, "non-zero exit code");
    }

    // Try unbinding.
    zx_status_t status =
        zx_task_bind_exception_port(child, ZX_HANDLE_INVALID, 0, options);
    if (bind_while_alive) {
        EXPECT_EQ(status, ZX_OK, "matched unbind should have succeeded");
    } else {
        EXPECT_NE(status, ZX_OK, "unmatched unbind should have failed");
    }

    // Clean up.
    tu_handle_close(child);
    if (eport != ZX_HANDLE_INVALID) {
        tu_handle_close(eport);
    }
    tu_handle_close(our_channel);
    return true;
}

static bool dead_process_matched_unbind_succeeds_test(void) {
    BEGIN_TEST;
    // If an eport is bound while a process is alive, it should be
    // valid to unbind it after the process is dead.
    ASSERT_TRUE(dead_process_unbind_helper(
        /* debugger */ false, /* bind_while_alive */ true), "");
    END_TEST;
}

static bool dead_process_mismatched_unbind_fails_test(void) {
    BEGIN_TEST;
    // If an eport was not bound while a process was alive, it should be
    // invalid to unbind it after the process is dead.
    ASSERT_TRUE(dead_process_unbind_helper(
        /* debugger */ false, /* bind_while_alive */ false), "");
    END_TEST;
}

static bool dead_process_debugger_matched_unbind_succeeds_test(void) {
    BEGIN_TEST;
    // If a debugger port is bound while a process is alive, it should be
    // valid to unbind it after the process is dead.
    ASSERT_TRUE(dead_process_unbind_helper(
        /* debugger */ true, /* bind_while_alive */ true), "");
    END_TEST;
}

static bool dead_process_debugger_mismatched_unbind_fails_test(void) {
    BEGIN_TEST;
    // If an eport was not bound while a process was alive, it should be
    // invalid to unbind it after the process is dead.
    ASSERT_TRUE(dead_process_unbind_helper(
        /* debugger */ true, /* bind_while_alive */ false), "");
    END_TEST;
}

// Creates a thread, possibly binds an eport to it (if |bind_while_alive| is set),
// then tries to unbind the eport, checking for the expected status.
static bool dead_thread_unbind_helper(bool bind_while_alive) {
    // Start a new thread.
    zx_handle_t our_channel, their_channel;
    tu_channel_create(&our_channel, &their_channel);
    thrd_t cthread;
    tu_thread_create_c11(&cthread, thread_func, (void*)(uintptr_t)their_channel,
                         "thread-set-close-set");
    zx_handle_t thread = thrd_get_zx_handle(cthread);
    ASSERT_NE(thread, ZX_HANDLE_INVALID, "failed to get thread handle");

    // Duplicate the thread's handle. thrd_join() will close the |thread|
    // handle, but we need to be able to refer to the thread after that.
    zx_handle_t thread_copy = tu_handle_duplicate(thread);

    // Possibly bind an eport to it.
    zx_handle_t eport = ZX_HANDLE_INVALID;
    if (bind_while_alive) {
        eport = tu_io_port_create();
        tu_set_exception_port(thread, eport, EXCEPTION_PORT_KEY, 0);
    }

    // Tell the thread to exit and wait for it.
    send_msg(our_channel, MSG_DONE);
    // thrd_join doesn't provide a timeout, but we have the watchdog for that.
    thrd_join(cthread, NULL);

    // Try unbinding.
    zx_status_t status =
        zx_task_bind_exception_port(thread_copy, ZX_HANDLE_INVALID, 0, 0);
    if (bind_while_alive) {
        EXPECT_EQ(status, ZX_OK, "matched unbind should have succeeded");
    } else {
        EXPECT_NE(status, ZX_OK, "unmatched unbind should have failed");
    }

    // Clean up. The |thread| and |their_channel| handles died
    // along with the thread.
    tu_handle_close(thread_copy);
    if (eport != ZX_HANDLE_INVALID) {
        tu_handle_close(eport);
    }
    tu_handle_close(our_channel);
    return true;
}

static bool dead_thread_matched_unbind_succeeds_test(void) {
    BEGIN_TEST;
    // If an eport is bound while a thread is alive, it should be
    // valid to unbind it after the thread is dead.
    ASSERT_TRUE(dead_thread_unbind_helper(/* bind_while_alive */ true), "");
    END_TEST;
}

static bool dead_thread_mismatched_unbind_fails_test(void) {
    BEGIN_TEST;
    // If an eport was not bound while a thread was alive, it should be
    // invalid to unbind it after the thread is dead.
    ASSERT_TRUE(dead_thread_unbind_helper(/* bind_while_alive */ false), "");
    END_TEST;
}

static void finish_basic_test(zx_handle_t child,
                              zx_handle_t eport, zx_handle_t our_channel,
                              enum message crash_msg, uint32_t excp_port_type)
{
    send_msg(our_channel, crash_msg);

    zx_koid_t tid;
    if (read_and_verify_exception(eport, child, ZX_EXCP_FATAL_PAGE_FAULT, &tid)) {
        resume_thread_from_exception(child, tid, excp_port_type, ZX_RESUME_TRY_NEXT);
        tu_process_wait_signaled(child);
    }

    tu_handle_close(child);
    tu_handle_close(eport);
    tu_handle_close(our_channel);
}

static bool job_handler_test(void)
{
    BEGIN_TEST;

    zx_handle_t job = tu_job_create(zx_job_default());
    zx_handle_t child, our_channel;
    start_test_child(job, test_child_name, &child, &our_channel);
    zx_handle_t eport = tu_io_port_create();
    tu_set_exception_port(job, eport, EXCEPTION_PORT_KEY, 0);
    REGISTER_CRASH(child);

    finish_basic_test(child, eport, our_channel, MSG_CRASH, ZX_EXCEPTION_PORT_TYPE_JOB);
    tu_handle_close(job);
    END_TEST;
}

static bool grandparent_job_handler_test(void)
{
    BEGIN_TEST;

    zx_handle_t grandparent_job = tu_job_create(zx_job_default());
    zx_handle_t parent_job = tu_job_create(grandparent_job);
    zx_handle_t job = tu_job_create(parent_job);
    zx_handle_t child, our_channel;
    start_test_child(job, test_child_name, &child, &our_channel);
    zx_handle_t eport = tu_io_port_create();
    tu_set_exception_port(grandparent_job, eport, EXCEPTION_PORT_KEY, 0);
    REGISTER_CRASH(child);

    finish_basic_test(child, eport, our_channel, MSG_CRASH, ZX_EXCEPTION_PORT_TYPE_JOB);
    tu_handle_close(job);
    tu_handle_close(parent_job);
    tu_handle_close(grandparent_job);
    END_TEST;
}

static bool process_handler_test(void)
{
    BEGIN_TEST;
    unittest_printf("process exception handler basic test\n");

    zx_handle_t child, our_channel;
    start_test_child(zx_job_default(), test_child_name, &child, &our_channel);
    zx_handle_t eport = tu_io_port_create();
    tu_set_exception_port(child, eport, EXCEPTION_PORT_KEY, 0);
    REGISTER_CRASH(child);

    finish_basic_test(child, eport, our_channel, MSG_CRASH, ZX_EXCEPTION_PORT_TYPE_PROCESS);
    END_TEST;
}

static bool thread_handler_test(void)
{
    BEGIN_TEST;
    unittest_printf("thread exception handler basic test\n");

    zx_handle_t child, our_channel;
    start_test_child(zx_job_default(), test_child_name, &child, &our_channel);
    zx_handle_t eport = tu_io_port_create();
    send_msg(our_channel, MSG_CREATE_AUX_THREAD);
    zx_handle_t thread;
    recv_msg_new_thread_handle(our_channel, &thread);
    if (thread != ZX_HANDLE_INVALID) {
        tu_set_exception_port(thread, eport, EXCEPTION_PORT_KEY, 0);
        REGISTER_CRASH(child);
        finish_basic_test(child, eport, our_channel, MSG_CRASH_AUX_THREAD, ZX_EXCEPTION_PORT_TYPE_THREAD);
        tu_handle_close(thread);
    } else {
        zx_task_kill(child);
        ASSERT_NE(thread, ZX_HANDLE_INVALID, "");
    }

    END_TEST;
}

static bool debugger_handler_test(void)
{
    BEGIN_TEST;
    unittest_printf("debugger exception handler basic test\n");

    zx_handle_t child, our_channel;
    start_test_child(zx_job_default(), test_child_name, &child, &our_channel);

    // We're binding to the debugger exception port so make sure the
    // child is running first so that we don't have to process
    // ZX_EXCP_THREAD_STARTING.
    ASSERT_TRUE(ensure_child_running(our_channel), "");

    zx_handle_t eport = tu_io_port_create();
    tu_set_exception_port(child, eport, EXCEPTION_PORT_KEY, ZX_EXCEPTION_PORT_DEBUGGER);

    finish_basic_test(child, eport, our_channel, MSG_CRASH, ZX_EXCEPTION_PORT_TYPE_DEBUGGER);
    END_TEST;
}

static bool packet_pid_test(void)
{
    BEGIN_TEST;

    zx_handle_t child, eport, our_channel;
    start_test_child_with_eport(zx_job_default(), test_child_name,
                                &child, &eport, &our_channel);

    zx_info_handle_basic_t child_info;
    tu_handle_get_basic_info(child, &child_info);

    zx_port_packet_t start_packet;
    ASSERT_TRUE(read_packet(eport, &start_packet), "error reading start exception");
    ASSERT_TRUE(verify_exception(&start_packet, child, ZX_EXCP_THREAD_STARTING),
                "unexpected exception");
    zx_koid_t packet_pid = start_packet.exception.pid;
    zx_koid_t packet_tid = start_packet.exception.tid;

    EXPECT_EQ(child_info.koid, packet_pid, "packet pid mismatch");

    send_msg(our_channel, MSG_DONE);
    resume_thread_from_exception(child, packet_tid, ZX_EXCEPTION_PORT_TYPE_DEBUGGER, 0);
    wait_process_exit_from_debugger(eport, child, packet_tid);

    tu_handle_close(child);
    tu_handle_close(eport);
    tu_handle_close(our_channel);

    END_TEST;
}

// Check that zx_thread_read_state() and zx_thread_write_state() both
// return ZX_ERR_NOT_SUPPORTED.  This is used for testing the cases where a
// thread is paused in the ZX_EXCP_THREAD_STARTING or or
// ZX_EXCP_THREAD_EXITING states.
static bool check_read_or_write_regs_is_rejected(zx_handle_t process,
                                                 zx_handle_t tid)
{
    zx_handle_t thread;
    ASSERT_EQ(zx_object_get_child(process, tid, ZX_RIGHT_SAME_RIGHTS, &thread), ZX_OK, "");
    zx_thread_state_general_regs_t regs;
    EXPECT_EQ(zx_thread_read_state(thread, ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)),
              ZX_ERR_NOT_SUPPORTED, "");
    EXPECT_EQ(zx_thread_write_state(thread, ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)),
              ZX_ERR_NOT_SUPPORTED, "");
    ASSERT_EQ(zx_handle_close(thread), ZX_OK, "");
    return true;
}

// Test the behavior of zx_thread_read_state() and zx_thread_write_state()
// when a thread is paused in the ZX_EXCP_THREAD_STARTING or
// ZX_EXCP_THREAD_EXITING states.
//
// For ZX_EXCP_THREAD_EXITING, this tests the case where a thread is
// exiting without the whole process also exiting.
static bool thread_state_when_starting_or_exiting_test(void)
{
    BEGIN_TEST;

    zx_handle_t child, eport, our_channel;
    start_test_child_with_eport(zx_job_default(), test_child_name,
                                &child, &eport, &our_channel);

    // Wait for the ZX_EXCP_THREAD_STARTING message for the subprocess's
    // initial thread.
    zx_koid_t initial_tid;
    ASSERT_TRUE(read_and_verify_exception(eport, child, ZX_EXCP_THREAD_STARTING,
                                          &initial_tid), "");
    EXPECT_TRUE(check_read_or_write_regs_is_rejected(child, initial_tid), "");
    resume_thread_from_exception(child, initial_tid,
                                 ZX_EXCEPTION_PORT_TYPE_DEBUGGER, 0);

    // Tell the subprocess to create a second thread.
    send_msg(our_channel, MSG_CREATE_AUX_THREAD);
    // Wait for the ZX_EXCP_THREAD_STARTING message about that thread.
    zx_koid_t tid;
    ASSERT_TRUE(read_and_verify_exception(eport, child, ZX_EXCP_THREAD_STARTING,
                                          &tid), "");
    EXPECT_NE(tid, initial_tid, "");
    EXPECT_TRUE(check_read_or_write_regs_is_rejected(child, tid), "");
    resume_thread_from_exception(child, tid, ZX_EXCEPTION_PORT_TYPE_DEBUGGER,
                                 0);

    // Tell the second thread to exit.
    send_msg(our_channel, MSG_SHUTDOWN_AUX_THREAD);
    // Wait for the ZX_EXCP_THREAD_EXITING message about that thread.
    zx_koid_t tid2;
    ASSERT_TRUE(read_and_verify_exception(eport, child, ZX_EXCP_THREAD_EXITING,
                                          &tid2), "");
    EXPECT_EQ(tid2, tid, "");
    EXPECT_TRUE(check_read_or_write_regs_is_rejected(child, tid), "");

    // Clean up: Resume the thread so that the process can exit.
    zx_handle_t thread;
    ASSERT_EQ(zx_object_get_child(child, tid, ZX_RIGHT_SAME_RIGHTS, &thread),
              ZX_OK, "");
    ASSERT_EQ(zx_task_resume(thread, ZX_RESUME_EXCEPTION), ZX_OK, "");
    tu_handle_close(thread);
    // Clean up: Tell the process to exit and wait for it to exit.
    send_msg(our_channel, MSG_DONE);
    tu_process_wait_signaled(child);
    tu_handle_close(child);
    tu_handle_close(eport);
    tu_handle_close(our_channel);

    END_TEST;
}

static bool process_start_test(void)
{
    BEGIN_TEST;
    unittest_printf("process start test\n");

    zx_handle_t child, eport, our_channel;
    start_test_child_with_eport(zx_job_default(), test_child_name,
                                &child, &eport, &our_channel);

    zx_koid_t tid;
    if (read_and_verify_exception(eport, child, ZX_EXCP_THREAD_STARTING, &tid)) {
        send_msg(our_channel, MSG_DONE);
        resume_thread_from_exception(child, tid, ZX_EXCEPTION_PORT_TYPE_DEBUGGER, 0);
        wait_process_exit_from_debugger(eport, child, tid);
    }

    tu_handle_close(child);
    tu_handle_close(eport);
    tu_handle_close(our_channel);

    END_TEST;
}

// Verify ZX_PROCESS_TERMINATED comes through bound exception port
// via async wait.

static bool process_exit_notification_test(void)
{
    BEGIN_TEST;
    unittest_printf("process exit notification test\n");

    zx_handle_t child, our_channel;
    start_test_child(zx_job_default(), test_child_name, &child, &our_channel);

    zx_handle_t eport = tu_io_port_create();
    tu_set_exception_port(child, eport, EXCEPTION_PORT_KEY, 0);
    tu_object_wait_async(child, eport, ZX_PROCESS_TERMINATED);

    send_msg(our_channel, MSG_DONE);

    wait_process_exit(eport, child);

    tu_handle_close(child);
    tu_handle_close(eport);
    tu_handle_close(our_channel);

    END_TEST;
}

// Verify ZX_THREAD_TERMINATED comes through bound exception port
// via async wait.

static bool thread_exit_notification_test(void)
{
    BEGIN_TEST;
    unittest_printf("thread exit notification test\n");

    zx_handle_t our_channel, their_channel;
    tu_channel_create(&our_channel, &their_channel);
    zx_handle_t eport = tu_io_port_create();
    thrd_t thread;
    tu_thread_create_c11(&thread, thread_func, (void*) (uintptr_t) their_channel, "thread-gone-test-thread");
    zx_handle_t thread_handle = thrd_get_zx_handle(thread);

    // |thread_handle| isn't usable to us, the thread exits before we're done
    // with the handle. So make a copy.
    zx_handle_t thread_handle_copy = tu_handle_duplicate(thread_handle);

    // Attach to the thread exception report as we're testing for ZX_THREAD_TERMINATED
    // reports from the thread here.
    tu_set_exception_port(thread_handle_copy, eport, EXCEPTION_PORT_KEY, 0);
    tu_object_wait_async(thread_handle_copy, eport, ZX_THREAD_TERMINATED);

    send_msg(our_channel, MSG_DONE);

    zx_port_packet_t packet;
    ASSERT_TRUE(read_packet(eport, &packet), "");
    zx_koid_t tid = tu_get_koid(thread_handle_copy);
    ASSERT_TRUE(verify_signal(&packet, tid, ZX_THREAD_TERMINATED), "");

    // thrd_join doesn't provide a timeout, but we have the watchdog for that.
    thrd_join(thread, NULL);

    tu_handle_close(thread_handle_copy);
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

// ARM does not trap on integer divide-by-zero.
#if defined(__x86_64__)
static void __NO_RETURN trigger_integer_divide_by_zero(void)
{
    // Use an x86 division instruction (rather than doing division from C)
    // to ensure that the compiler does not convert the division into
    // something else.
    uint32_t result;
    __asm__ volatile("idivb %1"
                     : "=a"(result)
                     : "r"((uint8_t) 0), "a"((uint16_t) 1));
    trigger_unsupported();
}

static void __NO_RETURN trigger_sse_divide_by_zero(void)
{
    // Unmask all exceptions for SSE operations.
    uint32_t mxcsr = 0;
    __asm__ volatile("ldmxcsr %0" : : "m"(mxcsr));

    double a = 1;
    double b = 0;
    __asm__ volatile("divsd %1, %0" : "+x"(a) : "x"(b));
    trigger_unsupported();
}

static void __NO_RETURN trigger_x87_divide_by_zero(void)
{
    // Unmask all exceptions for x87 operations.
    uint16_t control_word = 0;
    __asm__ volatile("fldcw %0" : : "m"(control_word));

    double a = 1;
    double b = 0;
    __asm__ volatile("fldl %0\n"
                     "fdivl %1\n"
                     // Check for the pending exception.
                     "fwait\n"
                     : : "m"(a), "m"(b));
    trigger_unsupported();
}
#endif

static const struct {
    zx_excp_type_t type;
    const char* name;
    bool crashes;
    void __NO_RETURN (*trigger_function) (void);
} exceptions[] = {
    { ZX_EXCP_GENERAL, "general", false, trigger_general },
    { ZX_EXCP_FATAL_PAGE_FAULT, "page-fault", true, trigger_fatal_page_fault },
    { ZX_EXCP_UNDEFINED_INSTRUCTION, "undefined-insn", true, trigger_undefined_insn },
    { ZX_EXCP_SW_BREAKPOINT, "sw-bkpt", true, trigger_sw_bkpt },
    { ZX_EXCP_HW_BREAKPOINT, "hw-bkpt", false, trigger_hw_bkpt },
#if defined(__x86_64__)
    { ZX_EXCP_GENERAL, "integer-divide-by-zero", true, trigger_integer_divide_by_zero },
    { ZX_EXCP_GENERAL, "sse-divide-by-zero", true, trigger_sse_divide_by_zero },
    { ZX_EXCP_GENERAL, "x87-divide-by-zero", true, trigger_x87_divide_by_zero },
#endif
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
        zx_excp_type_t excp_type = exceptions[i].type;
        const char *excp_name = exceptions[i].name;
        zx_handle_t child, eport, our_channel;
        char* arg = tu_asprintf("trigger=%s", excp_name);
        start_test_child_with_eport(zx_job_default(), arg,
                                    &child, &eport, &our_channel);
        free(arg);

        if (exceptions[i].crashes) {
            REGISTER_CRASH(child);
        }

        zx_koid_t tid = ZX_KOID_INVALID;
        (void) read_and_verify_exception(eport, child, ZX_EXCP_THREAD_STARTING, &tid);
        resume_thread_from_exception(child, tid, ZX_EXCEPTION_PORT_TYPE_DEBUGGER, 0);

        zx_port_packet_t packet;
        if (read_packet(eport, &packet)) {
            // ZX_EXCP_THREAD_EXITING reports must normally be responded to.
            // However, when the process exits it kills all threads which will
            // kick them out of the ExceptionHandlerExchange. Thus there's no
            // need to resume them here.
            ASSERT_TRUE(ZX_PKT_IS_EXCEPTION(packet.type), "");
            if (packet.type != ZX_EXCP_THREAD_EXITING) {
                tid = packet.exception.tid;
                verify_exception(&packet, child, excp_type);
                resume_thread_from_exception(child, tid,
                                             ZX_EXCEPTION_PORT_TYPE_DEBUGGER,
                                             ZX_RESUME_TRY_NEXT);
                zx_koid_t tid2;
                if (read_and_verify_exception(eport, child, ZX_EXCP_THREAD_EXITING, &tid2)) {
                    ASSERT_EQ(tid2, tid, "exiting tid mismatch");
                }
            } else {
                EXPECT_EQ(packet.exception.tid, tid, "");
                // Either the process exited cleanly because the exception
                // is unsupported, or it exited because exception processing
                // finished and the kernel killed the process. Either way
                // the process is dead thus there's no need to resume the
                // thread.
            }

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

typedef struct {
    // The walkthrough stops at the grandparent job as we don't want
    // crashlogger to see the exception: causes excessive noise in test output.
    // It doesn't stop at the parent job as we want to exercise finding threads
    // of processes of child jobs.
    zx_handle_t grandparent_job;
    zx_handle_t parent_job;
    zx_handle_t job;

    // the test process
    zx_handle_t child;

    // the test thread and its koid
    zx_handle_t thread;
    zx_koid_t tid;

    zx_handle_t grandparent_job_eport;
    zx_handle_t parent_job_eport;
    zx_handle_t job_eport;
    zx_handle_t child_eport;
    zx_handle_t thread_eport;
    zx_handle_t debugger_eport;

    // the communication channel to the test process
    zx_handle_t our_channel;
} walkthrough_state_t;

static bool walkthrough_setup(walkthrough_state_t* state)
{
    memset(state, 0, sizeof(*state));

    state->grandparent_job = tu_job_create(zx_job_default());
    state->parent_job = tu_job_create(state->grandparent_job);
    state->job = tu_job_create(state->parent_job);

    state->grandparent_job_eport = tu_io_port_create();
    state->parent_job_eport = tu_io_port_create();
    state->job_eport = tu_io_port_create();
    state->child_eport = tu_io_port_create();
    state->thread_eport = tu_io_port_create();
    state->debugger_eport = tu_io_port_create();

    start_test_child(state->job, test_child_name,
                     &state->child, &state->our_channel);

    send_msg(state->our_channel, MSG_CREATE_AUX_THREAD);
    recv_msg_new_thread_handle(state->our_channel, &state->thread);
    ASSERT_NE(state->thread, ZX_HANDLE_INVALID, "");
    state->tid = tu_get_koid(state->thread);

    tu_set_exception_port(state->grandparent_job, state->grandparent_job_eport, EXCEPTION_PORT_KEY, 0);
    tu_set_exception_port(state->parent_job, state->parent_job_eport, EXCEPTION_PORT_KEY, 0);
    tu_set_exception_port(state->job, state->job_eport, EXCEPTION_PORT_KEY, 0);
    tu_set_exception_port(state->child, state->child_eport, EXCEPTION_PORT_KEY, 0);
    tu_set_exception_port(state->thread, state->thread_eport, EXCEPTION_PORT_KEY, 0);
    tu_set_exception_port(state->child, state->debugger_eport, EXCEPTION_PORT_KEY, ZX_EXCEPTION_PORT_DEBUGGER);

    // Non-debugger exception ports don't get synthetic exceptions like
    // ZX_EXCP_THREAD_STARTING. We have to trigger an architectural exception.
    send_msg(state->our_channel, MSG_CRASH_AUX_THREAD);
    return true;
}

static void walkthrough_close(zx_handle_t* handle)
{
    if (*handle != ZX_HANDLE_INVALID) {
        tu_handle_close(*handle);
        *handle = ZX_HANDLE_INVALID;
    }
}

static void walkthrough_teardown(walkthrough_state_t* state)
{
    zx_task_kill(state->child);
    tu_process_wait_signaled(state->child);

    walkthrough_close(&state->thread);
    walkthrough_close(&state->child);
    walkthrough_close(&state->our_channel);
    walkthrough_close(&state->job);
    walkthrough_close(&state->parent_job);
    walkthrough_close(&state->grandparent_job);

    walkthrough_close(&state->debugger_eport);
    walkthrough_close(&state->thread_eport);
    walkthrough_close(&state->child_eport);
    walkthrough_close(&state->job_eport);
    walkthrough_close(&state->parent_job_eport);
    walkthrough_close(&state->grandparent_job_eport);
}

static void walkthrough_read_and_verify_exception(const walkthrough_state_t* state,
                                                  zx_handle_t eport)
{
    zx_koid_t exception_tid;
    if (read_and_verify_exception(eport, state->child, ZX_EXCP_FATAL_PAGE_FAULT, &exception_tid)) {
        EXPECT_EQ(exception_tid, state->tid, "");
    }
}

// Set up every kind of handler (except the system, we can't touch it), and
// verify unbinding an exception port walks through each handler in the search
// list (except the system exception handler which we can't touch).

static bool unbind_walkthrough_by_reset_test(void)
{
    BEGIN_TEST;

    walkthrough_state_t state;
    if (!walkthrough_setup(&state))
        goto Fail;

    walkthrough_read_and_verify_exception(&state, state.debugger_eport);

    tu_set_exception_port(state.child, ZX_HANDLE_INVALID, 0, ZX_EXCEPTION_PORT_DEBUGGER);
    walkthrough_read_and_verify_exception(&state, state.thread_eport);

    tu_set_exception_port(state.thread, ZX_HANDLE_INVALID, 0, 0);
    walkthrough_read_and_verify_exception(&state, state.child_eport);

    tu_set_exception_port(state.child, ZX_HANDLE_INVALID, 0, 0);
    walkthrough_read_and_verify_exception(&state, state.job_eport);

    tu_set_exception_port(state.job, ZX_HANDLE_INVALID, 0, 0);
    walkthrough_read_and_verify_exception(&state, state.parent_job_eport);

    tu_set_exception_port(state.parent_job, ZX_HANDLE_INVALID, 0, 0);
    walkthrough_read_and_verify_exception(&state, state.grandparent_job_eport);

Fail:
    walkthrough_teardown(&state);

    END_TEST;
}

// Set up every kind of handler (except the system, we can't touch it), and
// verify closing an exception port walks through each handler in the search
// list (except the system exception handler which we can't touch).

static bool unbind_walkthrough_by_close_test(void)
{
    BEGIN_TEST;

    walkthrough_state_t state;
    if (!walkthrough_setup(&state))
        goto Fail;

    walkthrough_read_and_verify_exception(&state, state.debugger_eport);

    walkthrough_close(&state.debugger_eport);
    walkthrough_read_and_verify_exception(&state, state.thread_eport);

    walkthrough_close(&state.thread_eport);
    walkthrough_read_and_verify_exception(&state, state.child_eport);

    walkthrough_close(&state.child_eport);
    walkthrough_read_and_verify_exception(&state, state.job_eport);

    walkthrough_close(&state.job_eport);
    walkthrough_read_and_verify_exception(&state, state.parent_job_eport);

    walkthrough_close(&state.parent_job_eport);
    walkthrough_read_and_verify_exception(&state, state.grandparent_job_eport);

Fail:
    walkthrough_teardown(&state);

    END_TEST;
}

// This test is different than the walkthrough tests in that it tests
// successful resumption of the child after the debugger port closes.

static bool unbind_while_stopped_test(void)
{
    BEGIN_TEST;
    unittest_printf("unbind_while_stopped tests\n");

    zx_handle_t child, eport, our_channel;
    start_test_child_with_eport(zx_job_default(), test_child_name,
                                &child, &eport, &our_channel);

    {
        zx_koid_t tid;
        (void) read_and_verify_exception(eport, child, ZX_EXCP_THREAD_STARTING, &tid);
    }

    // Now unbind the exception port and wait for the child to cleanly exit.
    // If this doesn't work the thread will stay blocked, we'll timeout, and
    // the watchdog will trigger.
    tu_set_exception_port(child, ZX_HANDLE_INVALID, 0, ZX_EXCEPTION_PORT_DEBUGGER);
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

    zx_handle_t child, eport, our_channel;
    start_test_child_with_eport(zx_job_default(), test_child_name,
                                &child, &eport, &our_channel);

    zx_port_packet_t start_packet;
    // Assert reading the start packet succeeds because otherwise the rest
    // of the test is moot.
    ASSERT_TRUE(read_packet(eport, &start_packet), "error reading start exception");
    ASSERT_TRUE(verify_exception(&start_packet, child, ZX_EXCP_THREAD_STARTING),
                "unexpected exception");
    zx_koid_t tid = start_packet.exception.tid;

    zx_handle_t thread;
    zx_status_t status = zx_object_get_child(child, tid, ZX_RIGHT_SAME_RIGHTS, &thread);
    if (status < 0)
        tu_fatal("zx_object_get_child", status);

    // The thread may still be running: There's a window between sending the
    // exception report and the thread going to sleep that is exposed to us.
    // We want to verify the thread is still waiting for an exception after we
    // unbind, so wait for the thread to go to sleep before we unbind.
    // Note that there's no worry of this hanging due to our watchdog.
    zx_info_thread_t info;
    do {
        zx_nanosleep(0);
        info = tu_thread_get_info(thread);
    } while (info.state != ZX_THREAD_STATE_BLOCKED);

    // Unbind the exception port quietly, meaning to leave the thread
    // waiting for an exception response.
    tu_set_exception_port(child, ZX_HANDLE_INVALID, 0,
                          ZX_EXCEPTION_PORT_DEBUGGER | ZX_EXCEPTION_PORT_UNBIND_QUIETLY);

    // Rebind and fetch the exception report, it should match the one
    // we just got.

    tu_set_exception_port(child, eport, EXCEPTION_PORT_KEY, ZX_EXCEPTION_PORT_DEBUGGER);

    // Verify exception report matches current exception.
    zx_exception_report_t report;
    status = zx_object_get_info(thread, ZX_INFO_THREAD_EXCEPTION_REPORT, &report, sizeof(report), NULL, NULL);
    if (status < 0)
        tu_fatal("zx_object_get_info(ZX_INFO_THREAD_EXCEPTION_REPORT)", status);
    EXPECT_EQ(report.header.type, start_packet.type, "type mismatch");
    // The "thread-start" report is a synthetic exception and doesn't contain
    // any arch info yet, so we can't test report.context.arch.

    // Done verifying we got the same exception, send the child on its way
    // and tell it we're done.
    resume_thread_from_exception(child, tid, ZX_EXCEPTION_PORT_TYPE_DEBUGGER, 0);
    send_msg(our_channel, MSG_DONE);

    wait_process_exit_from_debugger(eport, child, tid);

    // We should still be able to get info on the thread.
    info = tu_thread_get_info(thread);
    EXPECT_EQ(info.state, ZX_THREAD_STATE_DEAD, "unexpected thread state");
    EXPECT_EQ(info.wait_exception_port_type, ZX_EXCEPTION_PORT_TYPE_NONE, "wrong exception port type at thread exit");

    tu_handle_close(thread);
    tu_handle_close(child);
    tu_handle_close(eport);
    tu_handle_close(our_channel);

    END_TEST;
}

static bool kill_while_stopped_at_start_test(void)
{
    BEGIN_TEST;
    unittest_printf("kill_while_stopped_at_start tests\n");

    zx_handle_t child, eport, our_channel;
    start_test_child_with_eport(zx_job_default(), test_child_name,
                                &child, &eport, &our_channel);

    zx_koid_t tid;
    if (read_and_verify_exception(eport, child, ZX_EXCP_THREAD_STARTING, &tid)) {
        // Now kill the thread and wait for the child to exit.
        // This assumes the inferior only has the one thread.
        // If this doesn't work the thread will stay blocked, we'll timeout, and
        // the watchdog will trigger.
        zx_handle_t thread;
        zx_status_t status = zx_object_get_child(child, tid, ZX_RIGHT_SAME_RIGHTS, &thread);
        if (status < 0)
            tu_fatal("zx_object_get_child", status);
        zx_task_kill(thread);
        tu_process_wait_signaled(child);

        // Keep the thread handle open until after we know the process has exited
        // to ensure the thread's handle lifetime doesn't affect process lifetime.
        tu_handle_close(thread);
    }

    tu_handle_close(child);
    tu_handle_close(eport);
    tu_handle_close(our_channel);

    END_TEST;
}

static void write_to_addr(void* addr)
{
    *(int*) addr = 42;
}

static bool death_test(void)
{
    BEGIN_TEST;

    int* addr = 0;
    ASSERT_DEATH(write_to_addr, addr, "registered death: write to address 0x0");

    END_TEST;
}

static bool self_death_test(void)
{
    BEGIN_TEST;

    REGISTER_CRASH(zx_thread_self());
    crash_me();

    END_TEST;
}

typedef struct thread_info {
    zx_handle_t our_channel, their_channel;
    zx_handle_t thread_handle;
} thread_info_t;

static bool multiple_threads_registered_death_test(void)
{
    BEGIN_TEST;

    const unsigned int num_threads = 5;

    thread_info_t thread_info[num_threads];

    // Create some threads and register them as expected to crash.
    // This tests the crash list can handle multiple registered
    // handles.
    for (unsigned int i = 0; i < num_threads; i++) {
        tu_channel_create(&thread_info[i].our_channel,
                          &thread_info[i].their_channel);
        thrd_t thread;
        tu_thread_create_c11(&thread, thread_func,
                             (void*)(uintptr_t)thread_info[i].their_channel,
                             "registered-death-thread");
        // Note: We're assuming the thread won't exit before we're done with
        // the result of thrd_get_zx_handle.
        thread_info[i].thread_handle = thrd_get_zx_handle(thread);
        REGISTER_CRASH(thread_info[i].thread_handle);
    }

    // Make each thread crash. As they are registered, they will be
    // silently handled by the crash handler and the test should complete
    // without error.
    for (unsigned int i = 0; i < num_threads; i++) {
        send_msg(thread_info[i].our_channel, MSG_CRASH);

        ASSERT_EQ(zx_object_wait_one(thread_info[i].thread_handle,
                                     ZX_THREAD_TERMINATED,
                                     zx_deadline_after(ZX_MSEC(500)), NULL),
                  ZX_OK, "failed to wait for thread termination");

        tu_handle_close(thread_info[i].thread_handle);
        tu_handle_close(thread_info[i].our_channel);
        tu_handle_close(thread_info[i].their_channel);
    }

    END_TEST;
}

static void __NO_RETURN test_child_exit_closing_excp_handle(void)
{
    unittest_printf("Exit closing excp handle starting.\n");

    // Test ZX-1544. Process termination closing the last handle of the eport
    // should not cause a panic.
    zx_handle_t eport = tu_io_port_create();
    tu_set_exception_port(zx_process_self(), eport, EXCEPTION_PORT_KEY, 0);
    exit(0);

    /* NOTREACHED */
}

static bool exit_closing_excp_handle_test(void)
{
    BEGIN_TEST;

    unittest_printf("Starting test child.\n");

    const char* test_child_path = program_path;
    const char verbosity_string[] = { 'v', '=', utest_verbosity_level + '0', '\0' };
    const char* const argv[] = {
        test_child_path,
        exit_closing_excp_handle_child_name,
        verbosity_string,
    };
    int argc = countof(argv);

    launchpad_t* lp = tu_launch_fdio_init(zx_job_default(),
                                          exit_closing_excp_handle_child_name,
                                          argc, argv,
                                          NULL, 0, NULL, NULL);
    zx_handle_t child = tu_launch_fdio_fini(lp);

    zx_signals_t signals = ZX_PROCESS_TERMINATED;
    zx_signals_t pending;
    zx_status_t result = tu_wait(1, &child, &signals, &pending, ZX_TIME_INFINITE);
    EXPECT_EQ(result, ZX_OK, "");
    EXPECT_TRUE(pending & ZX_PROCESS_TERMINATED, "");

    EXPECT_EQ(tu_process_get_return_code(child), 0, "");

    END_TEST;
}

BEGIN_TEST_CASE(exceptions_tests)
RUN_TEST(job_set_close_set_test);
RUN_TEST(process_set_close_set_test);
RUN_TEST(process_debugger_set_close_set_test);
RUN_TEST(thread_set_close_set_test);
RUN_TEST(non_running_process_set_close_set_test);
RUN_TEST(non_running_process_debugger_set_close_set_test);
RUN_TEST(non_running_thread_set_close_set_test);
RUN_TEST(dead_process_matched_unbind_succeeds_test);
RUN_TEST(dead_process_mismatched_unbind_fails_test);
RUN_TEST(dead_process_debugger_matched_unbind_succeeds_test);
RUN_TEST(dead_process_debugger_mismatched_unbind_fails_test);
RUN_TEST(dead_thread_matched_unbind_succeeds_test);
RUN_TEST(dead_thread_mismatched_unbind_fails_test);
RUN_TEST_ENABLE_CRASH_HANDLER(job_handler_test);
RUN_TEST_ENABLE_CRASH_HANDLER(grandparent_job_handler_test);
RUN_TEST_ENABLE_CRASH_HANDLER(process_handler_test);
RUN_TEST_ENABLE_CRASH_HANDLER(thread_handler_test);
RUN_TEST(packet_pid_test);
RUN_TEST(thread_state_when_starting_or_exiting_test);
RUN_TEST(process_start_test);
RUN_TEST(process_exit_notification_test);
RUN_TEST(thread_exit_notification_test);
RUN_TEST_ENABLE_CRASH_HANDLER(trigger_test);
RUN_TEST(unbind_walkthrough_by_reset_test);
RUN_TEST(unbind_walkthrough_by_close_test);
RUN_TEST(unbind_while_stopped_test);
RUN_TEST(unbind_rebind_while_stopped_test);
RUN_TEST(kill_while_stopped_at_start_test);
RUN_TEST(death_test);
RUN_TEST_ENABLE_CRASH_HANDLER(self_death_test);
RUN_TEST_ENABLE_CRASH_HANDLER(multiple_threads_registered_death_test);
RUN_TEST(exit_closing_excp_handle_test);
END_TEST_CASE(exceptions_tests)

static void scan_argv(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "v=", 2) == 0) {
            int verbosity = atoi(argv[i] + 2);
            unittest_set_verbosity_level(verbosity);
        } else if (strncmp(argv[i], "ts=", 3) == 0) {
            int scale = atoi(argv[i] + 3);
            tu_set_timeout_scale(scale);
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
    scan_argv(argc, argv);

    if (argc >= 2) {
        const char* excp_name = check_trigger(argc, argv);
        if (excp_name) {
            test_child_trigger(excp_name);
            return 0;
        }
        if (strcmp(argv[1], test_child_name) == 0) {
            test_child();
            return 0;
        }
        if (strcmp(argv[1], exit_closing_excp_handle_child_name) == 0) {
            test_child_exit_closing_excp_handle();
            /* NOTREACHED */
        }
    }

    tu_watchdog_start();

    bool success = unittest_run_all_tests(argc, argv);

    tu_watchdog_cancel();

    return success ? 0 : -1;
}
