// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// N.B. We can't fully test the system exception handler here as that would
// interfere with the global crash logger.
// TODO(dbort): A good place to test the system exception handler would be in
// the "core" tests.

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <type_traits>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/port.h>
#include <zircon/threads.h>

#include <fbl/algorithm.h>
#include <lib/test-exceptions/exception-catcher.h>
#include <lib/zx/channel.h>
#include <lib/zx/exception.h>
#include <lib/zx/job.h>
#include <lib/zx/port.h>
#include <lib/zx/process.h>
#include <lib/zx/task.h>
#include <lib/zx/thread.h>
#include <lib/zx/time.h>
#include <test-utils/test-utils.h>
#include <zxtest/zxtest.h>

#include <launchpad/launchpad.h>

static int thread_func(void* arg);

// argv[0]
static char* program_path;

// This is the key that is assigned to the port when bound.
// This value appears in |packet.key| in all exception messages.
static const uint64_t EXCEPTION_PORT_KEY = 0x6b6579; // "key"

// When sending user packets use this key so that read_packet() knows they're
// legit.
static const uint64_t USER_PACKET_KEY = 0xee75736572ee; // eeuseree

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

static void crash_me()
{
    volatile int* p = 0;
    *p = 42;
}

static void send_msg_new_thread_handle(zx_handle_t handle, zx_handle_t thread)
{
    // Note: The handle is transferred to the receiver.
    uint64_t data = MSG_AUX_THREAD_HANDLE;
    tu_channel_write(handle, 0, &data, sizeof(data), &thread, 1);
}

static void send_msg(zx_handle_t handle, message msg)
{
    uint64_t data = msg;
    tu_channel_write(handle, 0, &data, sizeof(data), NULL, 0);
}

static bool recv_msg(zx_handle_t handle, message* msg)
{
    uint64_t data;
    uint32_t num_bytes = sizeof(data);

    if (!tu_channel_wait_readable(handle)) {
        return false;
    }

    tu_channel_read(handle, 0, &data, &num_bytes, NULL, 0);
    if (num_bytes != sizeof(data)) {
        return false;
    }

    *msg = static_cast<message>(data);
    return true;
}

static void recv_msg_new_thread_handle(zx_handle_t handle, zx_handle_t* thread) {
    uint64_t data;
    uint32_t num_bytes = sizeof(data);

    ASSERT_TRUE(tu_channel_wait_readable(handle), "peer closed while trying to read message");

    uint32_t num_handles = 1;
    tu_channel_read(handle, 0, &data, &num_bytes, thread, &num_handles);
    ASSERT_EQ(num_bytes, sizeof(data));
    ASSERT_EQ(num_handles, 1u);

    ASSERT_EQ(static_cast<message>(data), MSG_AUX_THREAD_HANDLE);
}

// "resume" here means "tell the kernel we're done"
// This test assumes no presence of the "debugger API" and therefore we can't
// resume from a segfault. Such a test is for the debugger API anyway.

static void resume_thread_from_exception(zx_handle_t process, zx_koid_t tid,
                                         uint32_t excp_port_type, zx_handle_t eport,
                                         uint32_t flags) {
    zx_handle_t thread;
    zx_status_t status = zx_object_get_child(process, tid, ZX_RIGHT_SAME_RIGHTS, &thread);
    if (status < 0)
        tu_fatal("zx_object_get_child", status);

    zx_info_thread_t info = tu_thread_get_info(thread);
    EXPECT_EQ(info.state, ZX_THREAD_STATE_BLOCKED_EXCEPTION);
    if (excp_port_type != ZX_EXCEPTION_PORT_TYPE_NONE) {
        EXPECT_EQ(info.wait_exception_port_type, excp_port_type);
    }

    status = zx_task_resume_from_exception(thread, eport, flags);
    if (status < 0)
        tu_fatal("resume_thread_from_exception", status);
    zx_handle_close(thread);
}

// Wait for and receive a user packet, exception, or signal on |eport|.
static void read_packet(zx_handle_t eport, zx_port_packet_t* packet) {
    ASSERT_OK(zx_port_wait(eport, ZX_TIME_INFINITE, packet));
    if (ZX_PKT_IS_SIGNAL_ONE(packet->type)) {
    } else if (ZX_PKT_IS_USER(packet->type)) {
        ASSERT_EQ(packet->key, USER_PACKET_KEY);
    } else {
        ASSERT_TRUE(ZX_PKT_IS_EXCEPTION(packet->type));
        ASSERT_EQ(packet->key, EXCEPTION_PORT_KEY);
        ASSERT_OK(packet->status);
    }
}

static void verify_exception(const zx_port_packet_t* packet,
                             zx_handle_t process,
                             zx_excp_type_t expected_type) {
    ASSERT_EQ(packet->type, expected_type);
    EXPECT_EQ(packet->key, EXCEPTION_PORT_KEY);

    // Verify the exception was from |process|.
    if (process != ZX_HANDLE_INVALID) {
        zx_koid_t pid = tu_get_koid(process);
        EXPECT_EQ(pid, packet->exception.pid);
    }
}

static void verify_signal(const zx_port_packet_t* packet,
                          uint64_t key,
                          zx_signals_t expected_signals) {
    ASSERT_TRUE(ZX_PKT_IS_SIGNAL_ONE(packet->type));

    if (key != 0u)
        EXPECT_EQ(packet->key, key);
    EXPECT_TRUE(packet->signal.observed & expected_signals);
}

static void read_and_verify_exception(zx_handle_t eport,
                                      zx_handle_t process,
                                      zx_excp_type_t expected_type,
                                      zx_koid_t* tid) {
    zx_port_packet_t packet;
    ASSERT_NO_FATAL_FAILURES(read_packet(eport, &packet));
    *tid = packet.exception.tid;
    verify_exception(&packet, process, expected_type);
}

// Wait for a process to exit, and while it's exiting verify we get the
// expected exception reports.
// The caller must have attached an async-wait for |process| to |eport|.
// See start_test_child_with_eport().
// We may receive thread-exit reports while the process is terminating but
// any other kind of exception is an error.
// This may be used when attached to the process or debugger exception port.
static void wait_process_exit(zx_handle_t eport, zx_handle_t process) {
    zx_port_packet_t packet;
    zx_koid_t pid = tu_get_koid(process);

    for (;;) {
        ASSERT_NO_FATAL_FAILURES(read_packet(eport, &packet));
        // If we get a process exit signal then all threads have exited.
        // Any other signal packet is an error.
        if (ZX_PKT_IS_SIGNAL_ONE(packet.type)) {
            ASSERT_EQ(packet.key, pid);
            ASSERT_TRUE(packet.signal.observed & ZX_PROCESS_TERMINATED);
            break;
        }
        ASSERT_NO_FATAL_FAILURES(verify_exception(&packet, process, ZX_EXCP_THREAD_EXITING));
        // ZX_EXCP_THREAD_EXITING reports must normally be responded to.
        // However, when the process exits it kills all threads which will
        // kick them out of the ExceptionHandlerExchange. Thus there's no
        // need to resume them here.
    }

    // This isn't necessary, but it tests being able to wait on the process
    // handle directly, after having waited on it via |eport|.
    tu_process_wait_signaled(process);
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
static void wait_process_exit_from_debugger(zx_handle_t eport, zx_handle_t process, zx_koid_t tid) {
    bool tid_seen = false;
    zx_port_packet_t packet;
    zx_koid_t pid = tu_get_koid(process);

    ASSERT_NE(tid, ZX_KOID_INVALID);

    for (;;) {
        ASSERT_NO_FATAL_FAILURES(read_packet(eport, &packet));
        // If we get a process exit signal then all threads have exited.
        // Any other signal packet is an error.
        if (ZX_PKT_IS_SIGNAL_ONE(packet.type)) {
            ASSERT_EQ(packet.key, pid);
            ASSERT_TRUE(packet.signal.observed & ZX_PROCESS_TERMINATED);
            break;
        } else if (ZX_PKT_IS_USER(packet.type)) {
            continue;
        }
        ASSERT_NO_FATAL_FAILURES(verify_exception(&packet, process, ZX_EXCP_THREAD_EXITING));
        if (packet.exception.tid == tid)
            tid_seen = true;
        // ZX_EXCP_THREAD_EXITING reports must normally be responded to.
        // However, when the process exits it kills all threads which will
        // kick them out of the ExceptionHandlerExchange. So send this thread
        // on its way, but it's ok if the thread is gone.
        zx_handle_t thread;
        zx_status_t status = zx_object_get_child(process, tid, ZX_RIGHT_SAME_RIGHTS, &thread);
        if (status == ZX_OK) {
            status = zx_task_resume_from_exception(thread, eport, 0);
            if (status < 0) {
                // If the resume failed the thread must be dying or dead.
                EXPECT_EQ(status, ZX_ERR_BAD_STATE);
                EXPECT_TRUE(tu_thread_is_dying_or_dead(thread));
            }
            zx_handle_close(thread);
        }
    }

    EXPECT_TRUE(tid_seen, "missing ZX_EXCP_THREAD_EXITING report");

    // This isn't necessary, but it tests being able to wait on the process
    // handle directly, after having waited on it via |eport|.
    tu_process_wait_signaled(process);
}

static bool ensure_child_running(zx_handle_t channel) {
    // Note: This function is called from external threads and thus does
    // not use EXPECT_*/ASSERT_*.
    message msg;
    send_msg(channel, MSG_PING);
    if (!recv_msg(channel, &msg)) {
        return false;
    }
    if (msg != MSG_PONG) {
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
        message msg;
        if (!recv_msg(channel, &msg)) {
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
                    printf("previous thread connection not shutdown\n");
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
            printf("unknown message received: %d\n", msg);
            break;
        }
    }
}

static int thread_func(void* arg)
{
    zx_handle_t msg_channel = (zx_handle_t) (uintptr_t) arg;
    msg_loop(msg_channel);
    tu_handle_close(msg_channel);
    return 0;
}

static void __NO_RETURN test_child()
{
    zx_handle_t channel = zx_take_startup_handle(PA_USER0);
    if (channel == ZX_HANDLE_INVALID)
        tu_fatal("zx_take_startup_handle", ZX_ERR_BAD_HANDLE - 1000);
    msg_loop(channel);
    exit(0);
}

static launchpad_t* setup_test_child(zx_handle_t job, const char* arg,
                                     zx_handle_t* out_channel)
{
    zx_handle_t our_channel, their_channel;
    tu_channel_create(&our_channel, &their_channel);
    const char* test_child_path = program_path;
    const char* const argv[] = {
        test_child_path,
        arg,
    };
    int argc = fbl::count_of(argv);
    zx_handle_t handles[1] = { their_channel };
    uint32_t handle_ids[1] = { PA_USER0 };
    *out_channel = our_channel;
    launchpad_t* lp = tu_launch_fdio_init(job, test_child_name, argc, argv,
                                          NULL, 1, handles, handle_ids);
    return lp;
}

static void start_test_child(zx_handle_t job, const char* arg,
                             zx_handle_t* out_child, zx_handle_t* out_channel)
{
    launchpad_t* lp = setup_test_child(job, arg, out_channel);
    *out_child = tu_launch_fdio_fini(lp);
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
    tu_object_wait_async(child, eport, ZX_PROCESS_TERMINATED);
    *out_child = child;
    *out_eport = eport;
}

// Tests binding and unbinding behavior.
// |object| must be a valid job, process, or thread handle.
// |debugger| must only be set if |object| is a process handle. If set,
// tests the behavior of binding the debugger eport; otherwise, binds
// the non-debugger exception port.
static void test_set_close_set(zx_handle_t object, bool debugger) {
    ASSERT_NE(object, ZX_HANDLE_INVALID);
    uint32_t options = debugger ? ZX_EXCEPTION_PORT_DEBUGGER : 0;

    // Bind an exception port to the object.
    zx_handle_t eport = tu_io_port_create();
    ASSERT_OK(zx_task_bind_exception_port(object, eport, 0, options));

    // Try binding another exception port to the same object, which should fail.
    zx_handle_t eport2 = tu_io_port_create();
    ASSERT_EQ(zx_task_bind_exception_port(object, eport, 0, options), ZX_ERR_ALREADY_BOUND,
              "wrong result from setting already bound exception port");

    // Close the ports.
    tu_handle_close(eport2);
    tu_handle_close(eport);

    // Verify the close removed the previous handler by successfully
    // adding a new one.
    eport = tu_io_port_create();
    ASSERT_OK(zx_task_bind_exception_port(object, eport, 0, options));
    tu_handle_close(eport);

    // Try unbinding from an object without a bound port, which should fail.
    ASSERT_NOT_OK(zx_task_bind_exception_port(object, ZX_HANDLE_INVALID, 0, options));
}

TEST(ExceptionTest, JobSetCloseSet) {
    zx_handle_t job = tu_job_create(zx_job_default());
    test_set_close_set(job, /* debugger */ false);
    tu_handle_close(job);
}

TEST(ExceptionTest, ProcessSetCloseSet) {
    test_set_close_set(zx_process_self(), /* debugger */ false);
}

TEST(ExceptionTest, ProcessDebuggerSetCloseSet) {
    test_set_close_set(zx_process_self(), /* debugger */ true);
}

TEST(ExceptionTest, ThreadSetCloseSet) {
    zx_handle_t our_channel, their_channel;
    tu_channel_create(&our_channel, &their_channel);
    thrd_t thread;
    tu_thread_create_c11(&thread, thread_func, (void*)(uintptr_t)their_channel,
                         "thread-set-close-set");
    zx_handle_t thread_handle = thrd_get_zx_handle(thread);
    ASSERT_NO_FATAL_FAILURES(test_set_close_set(thread_handle, /* debugger */ false));
    send_msg(our_channel, MSG_DONE);
    // thrd_join doesn't provide a timeout, but we have the watchdog for that.
    thrd_join(thread, NULL);
}

struct proc_handles {
    zx_handle_t proc;
    zx_handle_t vmar;
};

// Creates but does not start a process, returning its handles in |*ph|.
static void create_non_running_process(const char* name, proc_handles* ph) {
    memset(ph, 0, sizeof(*ph));
    zx_status_t status = zx_process_create(
        zx_job_default(), name, strlen(name), 0, &ph->proc, &ph->vmar);
    ASSERT_OK(status);
    ASSERT_NE(ph->proc, ZX_HANDLE_INVALID);
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

TEST(ExceptionTest, NonRunningProcessSetCloseSet) {
    // Create but do not start a process.
    proc_handles ph;
    ASSERT_NO_FATAL_FAILURES(create_non_running_process(__func__, &ph));

    // Make sure binding and unbinding behaves.
    test_set_close_set(ph.proc, /* debugger */ false);

    close_proc_handles(&ph);
}

TEST(ExceptionTest, NonRunningProcessDebuggerSetCloseSet) {
    // Create but do not start a process.
    proc_handles ph;
    ASSERT_NO_FATAL_FAILURES(create_non_running_process(__func__, &ph));

    // Make sure binding and unbinding behaves.
    test_set_close_set(ph.proc, /* debugger */ true);

    close_proc_handles(&ph);
}

TEST(ExceptionTest, NonRunningThreadSetCloseSet) {
    // Create but do not start a process.
    proc_handles ph;
    ASSERT_NO_FATAL_FAILURES(create_non_running_process(__func__, &ph));

    // Create but do not start a thread in that process.
    zx_handle_t thread = ZX_HANDLE_INVALID;
    zx_status_t status =
        zx_thread_create(ph.proc, __func__, sizeof(__func__)-1, 0, &thread);
    ASSERT_OK(status);
    ASSERT_NE(thread, ZX_HANDLE_INVALID);

    // Make sure binding and unbinding behaves.
    test_set_close_set(thread, /* debugger */ false);

    tu_handle_close(thread);
    close_proc_handles(&ph);
}

// Creates a process, possibly binds an eport to it (if |bind_while_alive| is set),
// then tries to unbind the eport, checking for the expected status.
static void dead_process_unbind_helper(bool debugger, bool bind_while_alive) {
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
            ASSERT_TRUE(ensure_child_running(our_channel));
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
        ASSERT_NO_FATAL_FAILURES(wait_process_exit(eport, child));
    } else {
        ASSERT_EQ(tu_process_wait_exit(child), 0);
    }

    // Try unbinding.
    zx_status_t status =
        zx_task_bind_exception_port(child, ZX_HANDLE_INVALID, 0, options);
    if (bind_while_alive) {
        EXPECT_OK(status, "matched unbind should have succeeded");
    } else {
        EXPECT_NOT_OK(status, "unmatched unbind should have failed");
    }

    // Clean up.
    tu_handle_close(child);
    if (eport != ZX_HANDLE_INVALID) {
        tu_handle_close(eport);
    }
    tu_handle_close(our_channel);
}

TEST(ExceptionTest, DeadProcessMatchedUnbindSucceeds) {
    // If an eport is bound while a process is alive, it should be
    // valid to unbind it after the process is dead.
    dead_process_unbind_helper(/* debugger */ false, /* bind_while_alive */ true);
}

TEST(ExceptionTest, DeadProcessMismatchedUnbindFails) {
    // If an eport was not bound while a process was alive, it should be
    // invalid to unbind it after the process is dead.
    dead_process_unbind_helper(/* debugger */ false, /* bind_while_alive */ false);
}

TEST(ExceptionTest, DeadProcessDebuggerMatchedUnbindSucceeds) {
    // If a debugger port is bound while a process is alive, it should be
    // valid to unbind it after the process is dead.
    dead_process_unbind_helper(/* debugger */ true, /* bind_while_alive */ true);
}

TEST(ExceptionTest, DeadProcessDebuggerMismatchedUnbindFails) {
    // If an eport was not bound while a process was alive, it should be
    // invalid to unbind it after the process is dead.
    dead_process_unbind_helper(/* debugger */ true, /* bind_while_alive */ false);
}

// Creates a thread, possibly binds an eport to it (if |bind_while_alive| is set),
// then tries to unbind the eport, checking for the expected status.
static void dead_thread_unbind_helper(bool bind_while_alive) {
    // Start a new thread.
    zx_handle_t our_channel, their_channel;
    tu_channel_create(&our_channel, &their_channel);
    thrd_t cthread;
    tu_thread_create_c11(&cthread, thread_func, (void*)(uintptr_t)their_channel,
                         "thread-set-close-set");
    zx_handle_t thread = thrd_get_zx_handle(cthread);
    ASSERT_NE(thread, ZX_HANDLE_INVALID);

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
        EXPECT_OK(status, "matched unbind should have succeeded");
    } else {
        EXPECT_NOT_OK(status, "unmatched unbind should have failed");
    }

    // Clean up. The |thread| and |their_channel| handles died
    // along with the thread.
    tu_handle_close(thread_copy);
    if (eport != ZX_HANDLE_INVALID) {
        tu_handle_close(eport);
    }
    tu_handle_close(our_channel);
}

TEST(ExceptionTest, DeadThreadMatchedUnbindSucceeds) {
    // If an eport is bound while a thread is alive, it should be
    // valid to unbind it after the thread is dead.
    dead_thread_unbind_helper(/* bind_while_alive */ true);
}

TEST(ExceptionTest, DeadThreadMismatchedUnbindFails) {
    // If an eport was not bound while a thread was alive, it should be
    // invalid to unbind it after the thread is dead.
    dead_thread_unbind_helper(/* bind_while_alive */ false);
}

static void finish_basic_test(zx_handle_t child,
                              zx_handle_t eport, zx_handle_t our_channel,
                              message crash_msg, uint32_t excp_port_type)
{
    test_exceptions::ExceptionCatcher catcher(*zx::job::default_job());

    send_msg(our_channel, crash_msg);

    zx_koid_t tid;
    ASSERT_NO_FATAL_FAILURES(read_and_verify_exception(eport, child, ZX_EXCP_FATAL_PAGE_FAULT, &tid));
    resume_thread_from_exception(child, tid, excp_port_type, eport, ZX_RESUME_TRY_NEXT);
    ASSERT_OK(catcher.ExpectException(*zx::unowned_process(child)));
    tu_task_kill(child);
    tu_process_wait_signaled(child);

    tu_handle_close(child);
    tu_handle_close(eport);
    tu_handle_close(our_channel);
}

TEST(ExceptionTest, JobHandler) {
    zx_handle_t job = tu_job_create(zx_job_default());
    zx_handle_t child, our_channel;
    start_test_child(job, test_child_name, &child, &our_channel);
    zx_handle_t eport = tu_io_port_create();
    tu_set_exception_port(job, eport, EXCEPTION_PORT_KEY, 0);

    finish_basic_test(child, eport, our_channel, MSG_CRASH, ZX_EXCEPTION_PORT_TYPE_JOB);
    tu_handle_close(job);
}

void job_debug_handler_test_helper(zx_handle_t job, zx_handle_t eport_job_handle) {
    zx_handle_t child, our_channel;
    zx_handle_t eport = tu_io_port_create();
    tu_set_exception_port(eport_job_handle, eport, EXCEPTION_PORT_KEY, ZX_EXCEPTION_PORT_DEBUGGER);
    start_test_child(job, test_child_name, &child, &our_channel);

    zx_info_handle_basic_t child_info;
    tu_handle_get_basic_info(child, &child_info);

    zx_port_packet_t start_packet;
    ASSERT_NO_FATAL_FAILURES(read_packet(eport, &start_packet));
    ASSERT_NO_FATAL_FAILURES(verify_exception(&start_packet, child, ZX_EXCP_PROCESS_STARTING));
    zx_koid_t packet_pid = start_packet.exception.pid;
    zx_koid_t packet_tid = start_packet.exception.tid;

    EXPECT_EQ(child_info.koid, packet_pid);

    // set exception on process
    zx_handle_t eport_process = tu_io_port_create();
    tu_set_exception_port(child, eport_process, EXCEPTION_PORT_KEY, ZX_EXCEPTION_PORT_DEBUGGER);
    tu_object_wait_async(child, eport_process, ZX_PROCESS_TERMINATED);

    // resume thread from job debugger
    resume_thread_from_exception(child, packet_tid, ZX_EXCEPTION_PORT_TYPE_JOB_DEBUGGER, eport, 0);

    zx_port_packet_t start_packet_process;
    ASSERT_NO_FATAL_FAILURES(read_packet(eport_process, &start_packet_process));
    ASSERT_NO_FATAL_FAILURES(verify_exception(&start_packet_process, child,
                                              ZX_EXCP_THREAD_STARTING));
    packet_pid = start_packet.exception.pid;
    packet_tid = start_packet.exception.tid;

    EXPECT_EQ(child_info.koid, packet_pid);

    send_msg(our_channel, MSG_DONE);
    resume_thread_from_exception(child, packet_tid, ZX_EXCEPTION_PORT_TYPE_DEBUGGER, eport_process,
                                 0);
    wait_process_exit_from_debugger(eport_process, child, packet_tid);

    tu_handle_close(child);
    tu_handle_close(eport);
    tu_handle_close(our_channel);
}

TEST(ExceptionTest, NestedJobDebugHandler) {
    zx_handle_t job = tu_job_create(zx_job_default());
    zx_handle_t nested_job = tu_job_create(job);
    job_debug_handler_test_helper(nested_job, job);
    tu_handle_close(nested_job);
    tu_handle_close(job);
}

TEST(ExceptionTest, JobDebugHandler) {
    zx_handle_t job = tu_job_create(zx_job_default());
    job_debug_handler_test_helper(job, job);
    tu_handle_close(job);
}

TEST(ExceptionTest, GrandparentJobHandler) {
    zx_handle_t grandparent_job = tu_job_create(zx_job_default());
    zx_handle_t parent_job = tu_job_create(grandparent_job);
    zx_handle_t job = tu_job_create(parent_job);
    zx_handle_t child, our_channel;
    start_test_child(job, test_child_name, &child, &our_channel);
    zx_handle_t eport = tu_io_port_create();
    tu_set_exception_port(grandparent_job, eport, EXCEPTION_PORT_KEY, 0);

    finish_basic_test(child, eport, our_channel, MSG_CRASH, ZX_EXCEPTION_PORT_TYPE_JOB);
    tu_handle_close(job);
    tu_handle_close(parent_job);
    tu_handle_close(grandparent_job);
}

TEST(ExceptionTest, ProcessHandler) {
    zx_handle_t child, our_channel;
    start_test_child(zx_job_default(), test_child_name, &child, &our_channel);
    zx_handle_t eport = tu_io_port_create();
    tu_set_exception_port(child, eport, EXCEPTION_PORT_KEY, 0);

    finish_basic_test(child, eport, our_channel, MSG_CRASH, ZX_EXCEPTION_PORT_TYPE_PROCESS);
}

TEST(ExceptionTest, ThreadHandler) {
    zx_handle_t child, our_channel;
    start_test_child(zx_job_default(), test_child_name, &child, &our_channel);
    zx_handle_t eport = tu_io_port_create();
    send_msg(our_channel, MSG_CREATE_AUX_THREAD);
    zx_handle_t thread;
    ASSERT_NO_FATAL_FAILURES(recv_msg_new_thread_handle(our_channel, &thread));
    if (thread != ZX_HANDLE_INVALID) {
        tu_set_exception_port(thread, eport, EXCEPTION_PORT_KEY, 0);
        finish_basic_test(child, eport, our_channel, MSG_CRASH_AUX_THREAD,
                          ZX_EXCEPTION_PORT_TYPE_THREAD);
        tu_handle_close(thread);
    } else {
        zx_task_kill(child);
        ASSERT_NE(thread, ZX_HANDLE_INVALID);
    }
}

TEST(ExceptionTest, DebuggerHandler) {
    zx_handle_t child, our_channel;
    start_test_child(zx_job_default(), test_child_name, &child, &our_channel);

    // We're binding to the debugger exception port so make sure the
    // child is running first so that we don't have to process
    // ZX_EXCP_THREAD_STARTING.
    ASSERT_TRUE(ensure_child_running(our_channel));

    zx_handle_t eport = tu_io_port_create();
    tu_set_exception_port(child, eport, EXCEPTION_PORT_KEY, ZX_EXCEPTION_PORT_DEBUGGER);

    finish_basic_test(child, eport, our_channel, MSG_CRASH, ZX_EXCEPTION_PORT_TYPE_DEBUGGER);
}

TEST(ExceptionTest, PacketPid) {
    zx_handle_t child, eport, our_channel;
    start_test_child_with_eport(zx_job_default(), test_child_name,
                                &child, &eport, &our_channel);

    zx_info_handle_basic_t child_info;
    tu_handle_get_basic_info(child, &child_info);

    zx_port_packet_t start_packet;
    ASSERT_NO_FATAL_FAILURES(read_packet(eport, &start_packet));
    ASSERT_NO_FATAL_FAILURES(verify_exception(&start_packet, child, ZX_EXCP_THREAD_STARTING));
    zx_koid_t packet_pid = start_packet.exception.pid;
    zx_koid_t packet_tid = start_packet.exception.tid;

    EXPECT_EQ(child_info.koid, packet_pid);

    send_msg(our_channel, MSG_DONE);
    resume_thread_from_exception(child, packet_tid, ZX_EXCEPTION_PORT_TYPE_DEBUGGER, eport, 0);
    wait_process_exit_from_debugger(eport, child, packet_tid);

    tu_handle_close(child);
    tu_handle_close(eport);
    tu_handle_close(our_channel);
}

// Check that zx_thread_read_state() and zx_thread_write_state() both
// return ZX_ERR_NOT_SUPPORTED.  This is used for testing the cases where a
// thread is paused in the ZX_EXCP_THREAD_EXITING state.
static void check_read_or_write_regs_is_rejected(zx_handle_t process,
                                                 zx_koid_t tid) {
    zx_handle_t thread;
    ASSERT_OK(zx_object_get_child(process, tid, ZX_RIGHT_SAME_RIGHTS, &thread));
    zx_thread_state_general_regs_t regs;
    EXPECT_EQ(zx_thread_read_state(thread, ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)),
              ZX_ERR_NOT_SUPPORTED);
    EXPECT_EQ(zx_thread_write_state(thread, ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)),
              ZX_ERR_NOT_SUPPORTED);
    ASSERT_OK(zx_handle_close(thread));
}

// Test the behavior of zx_thread_read_state() and zx_thread_write_state()
// when a thread is paused in the ZX_EXCP_THREAD_STARTING or
// ZX_EXCP_THREAD_EXITING states.
//
// For ZX_EXCP_THREAD_EXITING, this tests the case where a thread is
// exiting without the whole process also exiting.
TEST(ExceptionTest, ThreadStateWhenStartingOrExiting) {
    zx_handle_t child, eport, our_channel;
    start_test_child_with_eport(zx_job_default(), test_child_name,
                                &child, &eport, &our_channel);

    // Wait for the ZX_EXCP_THREAD_STARTING message for the subprocess's
    // initial thread.
    zx_koid_t initial_tid;
    ASSERT_NO_FATAL_FAILURES(read_and_verify_exception(eport, child, ZX_EXCP_THREAD_STARTING,
                                                       &initial_tid));
    // Register r/w is verified in utest/debugger.
    resume_thread_from_exception(child, initial_tid, ZX_EXCEPTION_PORT_TYPE_DEBUGGER, eport, 0);

    // Tell the subprocess to create a second thread.
    send_msg(our_channel, MSG_CREATE_AUX_THREAD);
    // Wait for the ZX_EXCP_THREAD_STARTING message about that thread.
    zx_koid_t tid;
    ASSERT_NO_FATAL_FAILURES(read_and_verify_exception(eport, child, ZX_EXCP_THREAD_STARTING,
                                                       &tid));
    EXPECT_NE(tid, initial_tid);
    // Register r/w is verified in utest/debugger.
    resume_thread_from_exception(child, tid, ZX_EXCEPTION_PORT_TYPE_DEBUGGER, eport, 0);

    // Tell the second thread to exit.
    send_msg(our_channel, MSG_SHUTDOWN_AUX_THREAD);
    // Wait for the ZX_EXCP_THREAD_EXITING message about that thread.
    zx_koid_t tid2;
    ASSERT_NO_FATAL_FAILURES(read_and_verify_exception(eport, child, ZX_EXCP_THREAD_EXITING,
                                                       &tid2));
    EXPECT_EQ(tid2, tid);
    ASSERT_NO_FATAL_FAILURES(check_read_or_write_regs_is_rejected(child, tid));

    // Clean up: Resume the thread so that the process can exit.
    zx_handle_t thread;
    ASSERT_OK(zx_object_get_child(child, tid, ZX_RIGHT_SAME_RIGHTS, &thread));
    ASSERT_OK(zx_task_resume_from_exception(thread, eport, 0));
    tu_handle_close(thread);
    // Clean up: Tell the process to exit and wait for it to exit.
    send_msg(our_channel, MSG_DONE);
    tu_process_wait_signaled(child);
    tu_handle_close(child);
    tu_handle_close(eport);
    tu_handle_close(our_channel);
}

TEST(ExceptionTest, ProcessStart) {
    zx_handle_t child, eport, our_channel;
    start_test_child_with_eport(zx_job_default(), test_child_name,
                                &child, &eport, &our_channel);

    zx_koid_t tid;
    ASSERT_NO_FATAL_FAILURES(read_and_verify_exception(eport, child, ZX_EXCP_THREAD_STARTING,
                                                       &tid));
    send_msg(our_channel, MSG_DONE);
    resume_thread_from_exception(child, tid, ZX_EXCEPTION_PORT_TYPE_DEBUGGER, eport, 0);
    wait_process_exit_from_debugger(eport, child, tid);

    tu_handle_close(child);
    tu_handle_close(eport);
    tu_handle_close(our_channel);
}

// Verify ZX_PROCESS_TERMINATED comes through bound exception port
// via async wait.

TEST(ExceptionTest, ProcessExitNotification) {
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
}

// Verify ZX_THREAD_TERMINATED comes through bound exception port
// via async wait.

TEST(ExceptionTest, ThreadExitNotification) {
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
    ASSERT_NO_FATAL_FAILURES(read_packet(eport, &packet));
    zx_koid_t tid = tu_get_koid(thread_handle_copy);
    ASSERT_NO_FATAL_FAILURES(verify_signal(&packet, tid, ZX_THREAD_TERMINATED));

    // thrd_join doesn't provide a timeout, but we have the watchdog for that.
    thrd_join(thread, NULL);

    tu_handle_close(thread_handle_copy);
    tu_handle_close(eport);
    tu_handle_close(our_channel);
}

static void __NO_RETURN trigger_unsupported()
{
    // An unsupported exception is not a failure.
    // Generally it just means that support for the exception doesn't
    // exist yet on this particular architecture.
    exit(0);
}

static void __NO_RETURN trigger_general()
{
#if defined(__x86_64__)
#elif defined(__aarch64__)
#endif
    trigger_unsupported();
}

static void __NO_RETURN trigger_fatal_page_fault()
{
    *(volatile int*) 0 = 42;
    trigger_unsupported();
}

static void __NO_RETURN trigger_undefined_insn()
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

static void __NO_RETURN trigger_sw_bkpt()
{
#if defined(__x86_64__)
    __asm__("int3");
#elif defined(__aarch64__)
    __asm__("brk 0");
#endif
    trigger_unsupported();
}

static void __NO_RETURN trigger_hw_bkpt()
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
static void __NO_RETURN trigger_integer_divide_by_zero()
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

static void __NO_RETURN trigger_sse_divide_by_zero()
{
    // Unmask all exceptions for SSE operations.
    uint32_t mxcsr = 0;
    __asm__ volatile("ldmxcsr %0" : : "m"(mxcsr));

    double a = 1;
    double b = 0;
    __asm__ volatile("divsd %1, %0" : "+x"(a) : "x"(b));

    // QEMU's software emulation of x86 appears to have a bug where it does
    // not correctly emulate generating division-by-zero exceptions from
    // SSE instructions.  See https://bugs.launchpad.net/qemu/+bug/1668041.
    // So we will reach this point on non-KVM QEMU.  In this case, make the
    // test pass by generating a fault by other means.
    //
    // That means this test isn't requiring that "divsd" generates a fault.
    // It is only requiring that the fault is handled properly
    // (e.g. doesn't cause a kernel panic) if the instruction does fault
    // (as on real hardware).
    printf("trigger_sse_divide_by_zero: divsd did not fault; "
           "assume we are running under a buggy non-KVM QEMU\n");
    trigger_integer_divide_by_zero();
}

static void __NO_RETURN trigger_x87_divide_by_zero()
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
    void __NO_RETURN (*trigger_function) ();
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
    for (size_t i = 0; i < fbl::count_of(exceptions); ++i)
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
    trigger_exception(excp_name);
    /* NOTREACHED */
}

TEST(ExceptionTest, Trigger) {
    for (size_t i = 0; i < fbl::count_of(exceptions); ++i) {
        zx_excp_type_t excp_type = exceptions[i].type;
        const char *excp_name = exceptions[i].name;
        zx_handle_t child, eport, our_channel;
        char* arg = tu_asprintf("trigger=%s", excp_name);
        start_test_child_with_eport(zx_job_default(), arg,
                                    &child, &eport, &our_channel);
        free(arg);

        test_exceptions::ExceptionCatcher catcher(*zx::job::default_job());

        zx_koid_t tid = ZX_KOID_INVALID;
        ASSERT_NO_FATAL_FAILURES(read_and_verify_exception(eport, child, ZX_EXCP_THREAD_STARTING,
                                                           &tid));
        resume_thread_from_exception(child, tid, ZX_EXCEPTION_PORT_TYPE_DEBUGGER, eport, 0);

        zx_port_packet_t packet;
        ASSERT_NO_FATAL_FAILURES(read_packet(eport, &packet));

        // ZX_EXCP_THREAD_EXITING reports must normally be responded to.
        // However, when the process exits it kills all threads which will
        // kick them out of the ExceptionHandlerExchange. Thus there's no
        // need to resume them here.
        ASSERT_TRUE(ZX_PKT_IS_EXCEPTION(packet.type));
        if (packet.type != ZX_EXCP_THREAD_EXITING) {
            tid = packet.exception.tid;
            ASSERT_NO_FATAL_FAILURES(verify_exception(&packet, child, excp_type));
            resume_thread_from_exception(child, tid, ZX_EXCEPTION_PORT_TYPE_DEBUGGER, eport,
                                         ZX_RESUME_TRY_NEXT);

            if (exceptions[i].crashes) {
                ASSERT_OK(catcher.ExpectException(*zx::unowned_process(child)));
                ASSERT_OK(zx_task_kill(child));
            }

            zx_koid_t tid2;
            ASSERT_NO_FATAL_FAILURES(read_and_verify_exception(eport, child,
                                                               ZX_EXCP_THREAD_EXITING, &tid2));
            ASSERT_EQ(tid2, tid);
        } else {
            EXPECT_EQ(packet.exception.tid, tid);
            // Either the process exited cleanly because the exception
            // is unsupported, or it exited because exception processing
            // finished and the kernel killed the process. Either way
            // the process is dead thus there's no need to resume the
            // thread.
        }

        // We've already seen tid's thread-exit report, so just skip that
        // test here.
        wait_process_exit(eport, child);

        tu_handle_close(child);
        tu_handle_close(eport);
        tu_handle_close(our_channel);
    }
}

struct walkthrough_state_t {
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
};

static void walkthrough_setup(walkthrough_state_t* state) {
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
    ASSERT_NO_FATAL_FAILURES(recv_msg_new_thread_handle(state->our_channel, &state->thread));
    ASSERT_NE(state->thread, ZX_HANDLE_INVALID);
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
    ASSERT_NO_FATAL_FAILURES(read_and_verify_exception(eport, state->child,
                                                       ZX_EXCP_FATAL_PAGE_FAULT, &exception_tid));
    EXPECT_EQ(exception_tid, state->tid);
}

// Set up every kind of handler (except the system, we can't touch it), and
// verify unbinding an exception port walks through each handler in the search
// list (except the system exception handler which we can't touch).

TEST(ExceptionTest, UnbindWalkthroughByReset) {
    walkthrough_state_t state;
    ASSERT_NO_FATAL_FAILURES(walkthrough_setup(&state));

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

    walkthrough_teardown(&state);
}

// Set up every kind of handler (except the system, we can't touch it), and
// verify closing an exception port walks through each handler in the search
// list (except the system exception handler which we can't touch).

TEST(ExceptionTest, UnbindWalkthroughByClose) {
    walkthrough_state_t state;
    ASSERT_NO_FATAL_FAILURES(walkthrough_setup(&state));

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

    walkthrough_teardown(&state);
}

// This test is different than the walkthrough tests in that it tests
// successful resumption of the child after the debugger port closes.

TEST(ExceptionTest, UnbindWhileStopped) {
    zx_handle_t child, eport, our_channel;
    start_test_child_with_eport(zx_job_default(), test_child_name,
                                &child, &eport, &our_channel);

    {
        zx_koid_t tid;
        ASSERT_NO_FATAL_FAILURES(read_and_verify_exception(eport, child, ZX_EXCP_THREAD_STARTING,
                                                           &tid));
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
}

TEST(ExceptionTest, KillProcessWhileStoppedAtStart) {
    zx_handle_t child, eport, our_channel;
    start_test_child_with_eport(zx_job_default(), test_child_name,
                                &child, &eport, &our_channel);

    zx_koid_t tid;
    ASSERT_NO_FATAL_FAILURES(read_and_verify_exception(eport, child, ZX_EXCP_THREAD_STARTING,
                                                       &tid));
    zx_handle_t thread = tu_process_get_thread(child, tid);

    tu_task_kill(child);

    // Even though we just killed the process, respond to the exception
    // to exercise ThreadDispatcher's unsignaling of the exception event.
    zx_status_t status = zx_task_resume_from_exception(thread, eport, 0);
    // Ideally we could control how the kernel schedules us and the
    // inferior, but we can't from userspace. Thus there's a race here,
    // either we get ZX_OK or we get ZX_ERR_BAD_STATE.
    // We want a failure here to print the value of |status|, without
    // getting excessively clever. That is why it is written this way.
    if (status != ZX_OK && status != ZX_ERR_BAD_STATE) {
        EXPECT_OK(status);
    }

    tu_process_wait_signaled(child);

    // Keep the thread handle open until after we know the process has exited
    // to ensure the thread's handle lifetime doesn't affect process lifetime.
    tu_handle_close(thread);

    tu_handle_close(child);
    tu_handle_close(eport);
    tu_handle_close(our_channel);
}

TEST(ExceptionTest, KillThreadWhileStoppedAtStart) {
    zx_handle_t child, eport, our_channel;
    start_test_child_with_eport(zx_job_default(), test_child_name,
                                &child, &eport, &our_channel);

    zx_koid_t tid;
    ASSERT_NO_FATAL_FAILURES(read_and_verify_exception(eport, child, ZX_EXCP_THREAD_STARTING,
                                                       &tid));
    // Now kill the thread and wait for the child to exit.
    // This assumes the inferior only has the one thread.
    // If this doesn't work the thread will stay blocked, we'll timeout, and
    // the watchdog will trigger.
    zx_handle_t thread;
    ASSERT_OK(zx_object_get_child(child, tid, ZX_RIGHT_SAME_RIGHTS, &thread));
    tu_task_kill(thread);
    tu_process_wait_signaled(child);

    // Keep the thread handle open until after we know the process has exited
    // to ensure the thread's handle lifetime doesn't affect process lifetime.
    tu_handle_close(thread);

    tu_handle_close(child);
    tu_handle_close(eport);
    tu_handle_close(our_channel);
}

static void __NO_RETURN test_child_exit_closing_excp_handle()
{
    // Test ZX-1544. Process termination closing the last handle of the eport
    // should not cause a panic.
    zx_handle_t eport = tu_io_port_create();
    tu_set_exception_port(zx_process_self(), eport, EXCEPTION_PORT_KEY, 0);
    exit(0);

    /* NOTREACHED */
}

TEST(ExceptionTest, ExitClosingExcpHandle) {
    const char* test_child_path = program_path;
    const char* const argv[] = {
        test_child_path,
        exit_closing_excp_handle_child_name,
    };
    int argc = fbl::count_of(argv);

    launchpad_t* lp = tu_launch_fdio_init(zx_job_default(),
                                          exit_closing_excp_handle_child_name,
                                          argc, argv,
                                          NULL, 0, NULL, NULL);
    zx_handle_t child = tu_launch_fdio_fini(lp);

    zx_signals_t signals = ZX_PROCESS_TERMINATED;
    zx_signals_t pending;
    EXPECT_OK(tu_wait(1, &child, &signals, &pending));
    EXPECT_TRUE(pending & ZX_PROCESS_TERMINATED);

    EXPECT_EQ(tu_process_get_return_code(child), 0);
}

namespace {

// Same as send_msg() but also allows ZX_ERR_PEER_CLOSED.
// Useful for generic test cleanup to handle both live and killed tasks.
static void SendMessageOrPeerClosed(const zx::channel& channel, message msg) {
    uint64_t data = msg;
    zx_status_t status = channel.write(0, &data, sizeof(data), nullptr, 0);
    if (status != ZX_OK && status != ZX_ERR_PEER_CLOSED) {
        tu_fatal(__func__, status);
    }
}

// C++ wrapper for our testing message loop to remove common boilerplate.
//
// Creates this test loop task structure under the current job:
//   - parent job
//     - job
//       - process
//         - thread
//         - aux thread
class TestLoop {
public:
    enum class Control {
        kAutomatic,
        kManual
    };

    // TestLoop can operate in two different modes:
    //
    // Automatic control will take care of all the setup/teardown so that when
    // this constructor returns the test threads will be running, and when
    // the destructor is called they will be stopped and closed down.
    //
    // Manual control requires the caller to make the following calls in order:
    //   - Step1CreateProcess()
    //   - Step2StartThreads()
    //   - Step3FinishSetup()
    //   - Step4ShutdownAuxThread()
    //   - Step5ShutdownMainThread()
    // This is necessary to give the caller a chance to install exception
    // handlers in between each step, e.g. in order to catch THREAD_STARTING
    // synthetic exceptions.
    TestLoop(Control control = Control::kAutomatic) {
        EXPECT_OK(zx::job::create(*zx::job::default_job(), 0, &parent_job_));
        EXPECT_OK(zx::job::create(parent_job_, 0, &job_));

        if (control == Control::kAutomatic) {
            Step1CreateProcess();
            Step2StartThreads();
            Step3ReadAuxThreadHandle();
        }
    }

    void Step1CreateProcess() {
        launchpad_ = setup_test_child(job_.get(), test_child_name,
                                      process_channel_.reset_and_get_address());
        ASSERT_NOT_NULL(launchpad_);
        process_.reset(launchpad_get_process_handle(launchpad_));
    }

    void Step2StartThreads() {
        // The initial process handle we got is invalidated by this call
        // and we're given the new one to use instead.
        process_.reset(tu_launch_fdio_fini(launchpad_));
        ASSERT_TRUE(process_.is_valid());
        send_msg(process_channel_.get(), MSG_CREATE_AUX_THREAD);
    }

    // If there are any debugger handlers attached, the task start exceptions
    // must be handled before calling this or it will block forever.
    void Step3ReadAuxThreadHandle() {
        recv_msg_new_thread_handle(process_channel_.get(), aux_thread_.reset_and_get_address());
    }

    void Step4ShutdownAuxThread() {
        // Don't use use zx_task_kill() here, it stops exception processing
        // immediately so we may miss expected exceptions.
        SendMessageOrPeerClosed(process_channel_, MSG_SHUTDOWN_AUX_THREAD);
    }

    void Step5ShutdownMainThread() {
        SendMessageOrPeerClosed(process_channel_, MSG_DONE);
    }

    // Closes the test tasks and blocks until everything has cleaned up.
    //
    // If there is an active debug handler, the process must be closed first
    // via zx_task_kill() or Shutdown(), or else this can block forever waiting
    // for the thread exit exceptions to be handled.
    ~TestLoop() {
        // It's OK to call these multiple times so we can just unconditionally
        // call them in both automatic or manual control mode.
        Step4ShutdownAuxThread();
        Step5ShutdownMainThread();

        EXPECT_OK(process_.wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), nullptr));
    }

    const zx::job& parent_job() const { return parent_job_; }
    const zx::job& job() const { return job_; }
    const zx::process& process() const { return process_; }
    const zx::thread& aux_thread() const { return aux_thread_; }

    // Sends a message to the aux thread to crash itself.
    //
    // If this is used, before exiting the test either kill the aux thread or
    // pass the exception to the unittest crash handler and block until it
    // kills the thread.
    //
    // The blocking is important because otherwise there's a race where the loop
    // process main thread can exit and kill the aux thread before the crash
    // handler gets a chance to see the exception. If this happens, the crash
    // handler will notice there was a registered exception that never occurred
    // and will fail the test.
    void CrashAuxThread() {
        send_msg(process_channel_.get(), MSG_CRASH_AUX_THREAD);
    }

private:
    launchpad_t* launchpad_ = nullptr;
    zx::job parent_job_;
    zx::job job_;
    zx::process process_;
    zx::channel process_channel_;
    zx::thread aux_thread_;
};

// Reads an exception for the given exception type.
// If |info| is non-null, fills it in with the received struct.
//
// Returns an invalid exception and marks test failure on error or if |type|
// doesn't match.
zx::exception ReadException(const zx::channel& channel, zx_excp_type_t type,
                            zx_exception_info_t* info_out = nullptr) {
    zx::exception exception;
    zx_exception_info_t info;
    uint32_t num_handles = 1;
    uint32_t num_bytes = sizeof(info);

    zx_status_t status = channel.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), nullptr);
    if (status != ZX_OK) {
        EXPECT_OK(status);
        return zx::exception();
    }

    tu_channel_read(channel.get(), 0, &info, &num_bytes, exception.reset_and_get_address(),
                    &num_handles);
    if (!exception.is_valid()) {
        EXPECT_TRUE(exception.is_valid());
        return zx::exception();
    }

    if (info_out != nullptr) {
        *info_out = info;
    }

    if (type != info.type) {
        EXPECT_EQ(type, info.type);
        return zx::exception();
    }
    return exception;
}

// Returns true if the exception has a thread handle. If |koid| is given,
// also checks that the thread's koid matches it.
bool ExceptionHasThread(const zx::exception& exception, zx_koid_t koid = ZX_KOID_INVALID) {
    zx::thread thread;
    if (exception.get_thread(&thread) != ZX_OK) {
        return false;
    }
    return koid == ZX_KOID_INVALID || koid == tu_get_koid(thread.get());
}

// Returns true if the exception has a process handle. If |koid| is given,
// also checks that the process' koid matches it.
bool ExceptionHasProcess(const zx::exception& exception, zx_koid_t koid = ZX_KOID_INVALID) {
    zx::process process;
    if (exception.get_process(&process) != ZX_OK) {
        return false;
    }
    return koid == ZX_KOID_INVALID || koid == tu_get_koid(process.get());
}

uint32_t GetExceptionStateProperty(const zx::exception& exception) {
    uint32_t state = ~0;
    EXPECT_OK(exception.get_property(ZX_PROP_EXCEPTION_STATE, &state, sizeof(state)));
    return state;
}

void SetExceptionStateProperty(const zx::exception& exception, uint32_t state) {
    ASSERT_OK(exception.set_property(ZX_PROP_EXCEPTION_STATE, &state, sizeof(state)));
}

// A finite timeout to use when you want to make sure something isn't happening
// e.g. a certain signal isn't going to be asserted.
auto constexpr kTestTimeout = zx::msec(50);

TEST(ExceptionTest, CreateExceptionChannel) {
    TestLoop loop;

    zx::channel exception_channel;
    ASSERT_OK(loop.aux_thread().create_exception_channel(0u, &exception_channel));
    EXPECT_TRUE(exception_channel.is_valid());
}

TEST(ExceptionTest, CreateExceptionChannelRights) {
    TestLoop loop;

    zx::channel exception_channel;
    ASSERT_OK(loop.aux_thread().create_exception_channel(0, &exception_channel));

    zx_info_handle_basic_t info;
    ASSERT_OK(exception_channel.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info),
                                         nullptr, nullptr));

    // If this set of rights ever changes make sure to adjust the
    // task_create_exception_channel() documentation as well.
    EXPECT_EQ(info.rights, ZX_RIGHT_TRANSFER | ZX_RIGHT_WAIT | ZX_RIGHT_READ);
}

TEST(ExceptionTest, CreateExceptionChannelInvalidArgs) {
    TestLoop loop;

    zx::channel exception_channel;
    EXPECT_EQ(loop.aux_thread().create_exception_channel(ZX_EXCEPTION_CHANNEL_DEBUGGER,
                                                         &exception_channel),
              ZX_ERR_INVALID_ARGS);
}

TEST(ExceptionTest, ProcessDebuggerAttached) {
    TestLoop loop;

    zx_info_process_t info;
    ASSERT_OK(loop.process().get_info(ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr));
    EXPECT_FALSE(info.debugger_attached);

    {
        zx::channel exception_channel;
        ASSERT_OK(loop.process().create_exception_channel(ZX_EXCEPTION_CHANNEL_DEBUGGER,
                                                          &exception_channel));

        ASSERT_OK(loop.process().get_info(ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr));
        EXPECT_TRUE(info.debugger_attached);
    }

    ASSERT_OK(loop.process().get_info(ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr));
    EXPECT_FALSE(info.debugger_attached);
}

// Removes a right from a task and ensures that channel creation now fails.
//
// |task_func|: TestLoop member function to get the task.
// |right|: ZX_RIGHT_* value to remove.
template <auto task_func>
void TaskRequiresRight(zx_rights_t right) {
    TestLoop loop;
    const auto& task = (loop.*task_func)();

    zx_info_handle_basic_t info;
    tu_handle_get_basic_info(task.get(), &info);

    auto reduced_task = typename std::remove_reference<decltype(task)>::type();
    ASSERT_OK(task.duplicate(info.rights & ~right, &reduced_task));

    zx::channel exception_channel;
    EXPECT_EQ(reduced_task.create_exception_channel(0, &exception_channel),
              ZX_ERR_ACCESS_DENIED);
}

TEST(ExceptionTest, ThreadRequiresRights) {
    ASSERT_NO_FAILURES(TaskRequiresRight<&TestLoop::aux_thread>(ZX_RIGHT_INSPECT));
    ASSERT_NO_FAILURES(TaskRequiresRight<&TestLoop::aux_thread>(ZX_RIGHT_DUPLICATE));
    ASSERT_NO_FAILURES(TaskRequiresRight<&TestLoop::aux_thread>(ZX_RIGHT_TRANSFER));
    ASSERT_NO_FAILURES(TaskRequiresRight<&TestLoop::aux_thread>(ZX_RIGHT_MANAGE_THREAD));
}

TEST(ExceptionTest, ProcessRequiresRights) {
    ASSERT_NO_FAILURES(TaskRequiresRight<&TestLoop::process>(ZX_RIGHT_INSPECT));
    ASSERT_NO_FAILURES(TaskRequiresRight<&TestLoop::process>(ZX_RIGHT_DUPLICATE));
    ASSERT_NO_FAILURES(TaskRequiresRight<&TestLoop::process>(ZX_RIGHT_TRANSFER));
    ASSERT_NO_FAILURES(TaskRequiresRight<&TestLoop::process>(ZX_RIGHT_MANAGE_THREAD));
    ASSERT_NO_FAILURES(TaskRequiresRight<&TestLoop::process>(ZX_RIGHT_ENUMERATE));
}

TEST(ExceptionTest, JobRequiresRights) {
    ASSERT_NO_FAILURES(TaskRequiresRight<&TestLoop::job>(ZX_RIGHT_INSPECT));
    ASSERT_NO_FAILURES(TaskRequiresRight<&TestLoop::job>(ZX_RIGHT_DUPLICATE));
    ASSERT_NO_FAILURES(TaskRequiresRight<&TestLoop::job>(ZX_RIGHT_TRANSFER));
    ASSERT_NO_FAILURES(TaskRequiresRight<&TestLoop::job>(ZX_RIGHT_MANAGE_THREAD));
    ASSERT_NO_FAILURES(TaskRequiresRight<&TestLoop::job>(ZX_RIGHT_ENUMERATE));
}

TEST(ExceptionTest, CreateSecondExceptionChannel) {
    TestLoop loop;
    zx::channel exception_channel;
    ASSERT_OK(loop.aux_thread().create_exception_channel(0u, &exception_channel));

    // Trying to register a second channel should fail.
    zx::channel exception_channel2;
    EXPECT_EQ(loop.aux_thread().create_exception_channel(0u, &exception_channel2),
              ZX_ERR_ALREADY_BOUND);
    EXPECT_FALSE(exception_channel2.is_valid());
}

TEST(ExceptionTest, OverwriteClosedExceptionChannel) {
    TestLoop loop;
    zx::channel exception_channel;
    ASSERT_OK(loop.aux_thread().create_exception_channel(0u, &exception_channel));

    // If we close the existing channel, registering a new one should succeed.
    exception_channel.reset();
    zx::channel exception_channel2;
    ASSERT_OK(loop.aux_thread().create_exception_channel(0u, &exception_channel2));
    EXPECT_TRUE(exception_channel2.is_valid());
}

// This is the basic test to receive an exception, parameterized so we can
// easily run it against all the different exception handler types.
//
// |task_func|: TestLoop member function to get the task.
// |create_flags|: flags to pass to zx_task_create_exception_channel().
// |expected_type|: expected exception type reported in zx_info_thread_t.
// |has_process|: true if the exception should have a process handle.
template <auto task_func>
void receive_test(uint32_t create_flags, uint32_t expected_type, bool has_process) {
    TestLoop loop;
    zx::channel exception_channel;
    ASSERT_OK((loop.*task_func)().create_exception_channel(create_flags, &exception_channel));

    loop.CrashAuxThread();
    zx_exception_info_t exception_info;
    zx::exception exception = ReadException(exception_channel, ZX_EXCP_FATAL_PAGE_FAULT,
                                            &exception_info);

    // Make sure exception info is correct.
    EXPECT_EQ(exception_info.tid, tu_get_koid(loop.aux_thread().get()));
    EXPECT_TRUE(ExceptionHasThread(exception, exception_info.tid));

    EXPECT_EQ(exception_info.pid, tu_get_koid(loop.process().get()));
    if (has_process) {
        EXPECT_TRUE(ExceptionHasProcess(exception, exception_info.pid));
    } else {
        EXPECT_FALSE(ExceptionHasProcess(exception));
    }

    // Make sure the thread state is correct.
    zx_info_thread_t thread_info = tu_thread_get_info(loop.aux_thread().get());
    EXPECT_EQ(thread_info.state, ZX_THREAD_STATE_BLOCKED_EXCEPTION);
    EXPECT_EQ(thread_info.wait_exception_port_type, expected_type);

    test_exceptions::ExceptionCatcher catcher(*zx::job::default_job());
    exception.reset();
    ASSERT_OK(catcher.ExpectException(loop.aux_thread()));
    EXPECT_OK(loop.aux_thread().wait_one(ZX_THREAD_TERMINATED, zx::time::infinite(), nullptr));
}

TEST(ExceptionTest, ThreadReceive) {
    receive_test<&TestLoop::aux_thread>(0, ZX_EXCEPTION_CHANNEL_TYPE_THREAD, false);
}

TEST(ExceptionTest, ProcessReceive) {
    receive_test<&TestLoop::process>(0, ZX_EXCEPTION_CHANNEL_TYPE_PROCESS, true);
}

TEST(ExceptionTest, ProcessDebuggerReceive) {
    receive_test<&TestLoop::process>(ZX_EXCEPTION_CHANNEL_DEBUGGER,
                                     ZX_EXCEPTION_CHANNEL_TYPE_DEBUGGER, true);
}

TEST(ExceptionTest, JobReceive) {
    receive_test<&TestLoop::job>(0, ZX_EXCEPTION_CHANNEL_TYPE_JOB, true);
}

TEST(ExceptionTest, JobDebuggerReceive) {
    receive_test<&TestLoop::parent_job>(0, ZX_EXCEPTION_CHANNEL_TYPE_JOB, true);
}

TEST(ExceptionTest, ExceptionResume) {
    TestLoop loop;
    zx::channel exception_channel;
    ASSERT_OK(loop.aux_thread().create_exception_channel(0u, &exception_channel));

    loop.CrashAuxThread();
    zx::exception exception = ReadException(exception_channel, ZX_EXCP_FATAL_PAGE_FAULT);

    // If we tell this exception to resume the thread, it should fault
    // again and return another exception back to us rather than
    // bubbling up the chain.
    SetExceptionStateProperty(exception, ZX_EXCEPTION_STATE_HANDLED);
    exception.reset();
    exception = ReadException(exception_channel, ZX_EXCP_FATAL_PAGE_FAULT);

    // Close the new exception without marking it handled so it bubbles up.
    test_exceptions::ExceptionCatcher catcher(loop.process());
    exception.reset();
    ASSERT_OK(catcher.ExpectException(loop.aux_thread()));
    EXPECT_OK(loop.aux_thread().wait_one(ZX_THREAD_TERMINATED, zx::time::infinite(), nullptr));
}

TEST(ExceptionTest, ExceptionStateProperty) {
    TestLoop loop;
    zx::channel exception_channel;
    ASSERT_OK(loop.aux_thread().create_exception_channel(0u, &exception_channel));

    loop.CrashAuxThread();
    zx::exception exception = ReadException(exception_channel, ZX_EXCP_FATAL_PAGE_FAULT);

    // By default exceptions should be unhandled.
    EXPECT_EQ(GetExceptionStateProperty(exception), ZX_EXCEPTION_STATE_TRY_NEXT);

    SetExceptionStateProperty(exception, ZX_EXCEPTION_STATE_HANDLED);
    EXPECT_EQ(GetExceptionStateProperty(exception), ZX_EXCEPTION_STATE_HANDLED);

    SetExceptionStateProperty(exception, ZX_EXCEPTION_STATE_TRY_NEXT);
    EXPECT_EQ(GetExceptionStateProperty(exception), ZX_EXCEPTION_STATE_TRY_NEXT);

    test_exceptions::ExceptionCatcher catcher(loop.process());
    exception.reset();
    ASSERT_OK(catcher.ExpectException(loop.aux_thread()));
    EXPECT_OK(loop.aux_thread().wait_one(ZX_THREAD_TERMINATED, zx::time::infinite(), nullptr));
}

TEST(ExceptionTest, ExceptionStatePropertyBadArgs) {
    TestLoop loop;
    zx::channel exception_channel;
    ASSERT_OK(loop.aux_thread().create_exception_channel(0u, &exception_channel));

    loop.CrashAuxThread();
    zx::exception exception = ReadException(exception_channel, ZX_EXCP_FATAL_PAGE_FAULT);

    // Wrong handle type.
    uint32_t state = ZX_EXCEPTION_STATE_HANDLED;
    EXPECT_EQ(loop.aux_thread().set_property(ZX_PROP_EXCEPTION_STATE, &state, sizeof(state)),
              ZX_ERR_WRONG_TYPE);
    EXPECT_EQ(loop.aux_thread().get_property(ZX_PROP_EXCEPTION_STATE, &state, sizeof(state)),
              ZX_ERR_WRONG_TYPE);

    // Illegal state value.
    state = ~0;
    EXPECT_EQ(exception.set_property(ZX_PROP_EXCEPTION_STATE, &state, sizeof(state)),
              ZX_ERR_INVALID_ARGS);

    // Buffer too short.
    state = ZX_EXCEPTION_STATE_HANDLED;
    EXPECT_EQ(exception.set_property(ZX_PROP_EXCEPTION_STATE, &state, sizeof(state) - 1),
              ZX_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(exception.get_property(ZX_PROP_EXCEPTION_STATE, &state, sizeof(state) - 1),
              ZX_ERR_BUFFER_TOO_SMALL);

    test_exceptions::ExceptionCatcher catcher(loop.process());
    exception.reset();
    ASSERT_OK(catcher.ExpectException(loop.aux_thread()));
    EXPECT_OK(loop.aux_thread().wait_one(ZX_THREAD_TERMINATED, zx::time::infinite(), nullptr));
}

TEST(ExceptionTest, CloseChannelWithException) {
    TestLoop loop;
    zx::channel exception_channel;
    ASSERT_OK(loop.aux_thread().create_exception_channel(0u, &exception_channel));

    loop.CrashAuxThread();
    ASSERT_OK(exception_channel.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), nullptr));

    // Closing the channel while it still contains the exception should pass
    // control to the next handler.
    test_exceptions::ExceptionCatcher catcher(loop.process());
    exception_channel.reset();
    ASSERT_OK(catcher.ExpectException(loop.aux_thread()));
    EXPECT_OK(loop.aux_thread().wait_one(ZX_THREAD_TERMINATED, zx::time::infinite(), nullptr));
}

TEST(ExceptionTest, CloseChannelWithoutException) {
    TestLoop loop;
    zx::channel exception_channel;
    ASSERT_OK(loop.aux_thread().create_exception_channel(0u, &exception_channel));

    loop.CrashAuxThread();
    zx::exception exception = ReadException(exception_channel, ZX_EXCP_FATAL_PAGE_FAULT);

    // Closing the channel after the exception object has been read out has no
    // effect since the exception object now controls the exception lifecycle.
    exception_channel.reset();

    // Wait a little bit to make sure the thread really is still blocked on our
    // exception object. If it wasn't, the exception would filter up now and
    // ExpectException() will deadlock when it fails to find the exception.
    zx::nanosleep(zx::deadline_after(kTestTimeout));

    test_exceptions::ExceptionCatcher catcher(loop.process());
    exception.reset();
    ASSERT_OK(catcher.ExpectException(loop.aux_thread()));
    EXPECT_OK(loop.aux_thread().wait_one(ZX_THREAD_TERMINATED, zx::time::infinite(), nullptr));
}

// Make sure a closed exception channel has no effect on other handlers.
TEST(ExceptionTest, SkipClosedExceptionChannel) {
    TestLoop loop;
    zx::channel job_channel, process_channel;
    ASSERT_OK(loop.job().create_exception_channel(0, &job_channel));
    ASSERT_OK(loop.process().create_exception_channel(0, &process_channel));

    {
        zx::channel thread_channel;
        ASSERT_OK(loop.aux_thread().create_exception_channel(0, &thread_channel));
    }

    loop.CrashAuxThread();

    // We should receive the exception on the process handler and it should
    // wait for our response as normal.
    {
        zx::exception exception = ReadException(process_channel, ZX_EXCP_FATAL_PAGE_FAULT);
        ASSERT_EQ(job_channel.wait_one(ZX_CHANNEL_READABLE, zx::deadline_after(kTestTimeout),
                                       nullptr),
                  ZX_ERR_TIMED_OUT);
    }

    // The exception should continue up to the job handler as normal.
    zx::exception exception = ReadException(job_channel, ZX_EXCP_FATAL_PAGE_FAULT);

    ASSERT_OK(loop.aux_thread().kill());
}

// Killing the task should mark its exception channels with PEER_CLOSED.
// Parameterized to more easily run it against the different handler types.
template <auto task_func>
void TaskDeathClosesExceptionChannel(uint32_t create_flags) {
    TestLoop loop;
    const auto& task = (loop.*task_func)();
    zx::channel exception_channel;
    ASSERT_OK(task.create_exception_channel(create_flags, &exception_channel));

    ASSERT_OK(task.kill());
    EXPECT_OK(exception_channel.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), nullptr));
}

TEST(ExceptionTest, TaskDeathClosesThreadExceptionChannel) {
    TaskDeathClosesExceptionChannel<&TestLoop::aux_thread>(0);
}

TEST(ExceptionTest, TaskDeathClosesProcessExceptionChannel) {
    TaskDeathClosesExceptionChannel<&TestLoop::process>(0);
}

TEST(ExceptionTest, TaskDeathClosesProcessDebugExceptionChannel) {
    TaskDeathClosesExceptionChannel<&TestLoop::process>(ZX_EXCEPTION_CHANNEL_DEBUGGER);
}

TEST(ExceptionTest, TaskDeathClosesJobExceptionChannel) {
    TaskDeathClosesExceptionChannel<&TestLoop::job>(0);
}

TEST(ExceptionTest, TaskDeathClosesJobDebugExceptionChannel) {
    TaskDeathClosesExceptionChannel<&TestLoop::job>(ZX_EXCEPTION_CHANNEL_DEBUGGER);
}

TEST(ExceptionTest, ThreadDeathWithExceptionInChannel) {
    TestLoop loop;
    zx::channel exception_channel;
    ASSERT_OK(loop.aux_thread().create_exception_channel(0u, &exception_channel));

    // Crash the thread and wait for the exception to be in the channel.
    loop.CrashAuxThread();
    ASSERT_OK(exception_channel.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), nullptr));

    // Killing the thread doesn't remove the exception from the channel, but
    // it does signal PEER_CLOSED.
    zx_signals_t observed;
    ASSERT_OK(loop.aux_thread().kill());
    EXPECT_OK(exception_channel.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), &observed));
    EXPECT_TRUE(observed & ZX_CHANNEL_READABLE);

    // Receiving and closing the exception has no effect. Operations on the
    // exception should still succeed.
    zx::exception exception = ReadException(exception_channel, ZX_EXCP_FATAL_PAGE_FAULT);
    uint32_t state = ZX_EXCEPTION_STATE_HANDLED;
    EXPECT_OK(exception.set_property(ZX_PROP_EXCEPTION_STATE, &state, sizeof(state)));
    EXPECT_OK(exception.get_property(ZX_PROP_EXCEPTION_STATE, &state, sizeof(state)));
}

// Similar to the above test, but pull the exception out of the channel before
// killing the thread and make sure behavior is consistent.
TEST(ExceptionTest, ThreadDeathWithExceptionReceived) {
    TestLoop loop;
    zx::channel exception_channel;
    ASSERT_OK(loop.aux_thread().create_exception_channel(0u, &exception_channel));

    loop.CrashAuxThread();
    zx::exception exception = ReadException(exception_channel, ZX_EXCP_FATAL_PAGE_FAULT);

    zx_signals_t observed;
    ASSERT_OK(loop.aux_thread().kill());
    ASSERT_OK(exception_channel.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), &observed));
    EXPECT_FALSE(observed & ZX_CHANNEL_READABLE);

    uint32_t state = ZX_EXCEPTION_STATE_HANDLED;
    EXPECT_OK(exception.set_property(ZX_PROP_EXCEPTION_STATE, &state, sizeof(state)));
    EXPECT_OK(exception.get_property(ZX_PROP_EXCEPTION_STATE, &state, sizeof(state)));
}

TEST(ExceptionTest, ExceptionChannelOrder) {
    TestLoop loop;

    // Set the exception channels up in the expected order.
    zx::channel exception_channels[5];
    ASSERT_OK(loop.process().create_exception_channel(ZX_EXCEPTION_CHANNEL_DEBUGGER,
                                                      &exception_channels[0]));
    ASSERT_OK(loop.aux_thread().create_exception_channel(0u, &exception_channels[1]));
    ASSERT_OK(loop.process().create_exception_channel(0u, &exception_channels[2]));
    ASSERT_OK(loop.job().create_exception_channel(0u, &exception_channels[3]));
    ASSERT_OK(loop.parent_job().create_exception_channel(0u, &exception_channels[4]));

    loop.CrashAuxThread();
    test_exceptions::ExceptionCatcher catcher(*zx::job::default_job());

    for (const zx::channel& channel : exception_channels) {
        ReadException(channel, ZX_EXCP_FATAL_PAGE_FAULT);
    }

    ASSERT_OK(catcher.ExpectException(loop.aux_thread()));
    EXPECT_OK(loop.aux_thread().wait_one(ZX_THREAD_TERMINATED, zx::time::infinite(), nullptr));
}

TEST(ExceptionTest, ThreadLifecycleChannelExceptions) {
    TestLoop loop(TestLoop::Control::kManual);

    loop.Step1CreateProcess();
    zx::channel exception_channel;
    ASSERT_OK(loop.process().create_exception_channel(ZX_EXCEPTION_CHANNEL_DEBUGGER,
                                                      &exception_channel));

    // We should get both primary and aux thread exceptions.
    loop.Step2StartThreads();

    zx_exception_info_t primary_start_info;
    {
        zx::exception exception = ReadException(exception_channel, ZX_EXCP_THREAD_STARTING,
                                                &primary_start_info);
        EXPECT_EQ(primary_start_info.pid, tu_get_koid(loop.process().get()));
        EXPECT_TRUE(ExceptionHasThread(exception, primary_start_info.tid));
        EXPECT_TRUE(ExceptionHasProcess(exception, primary_start_info.pid));
    }

    zx_exception_info_t aux_start_info;
    {
        zx::exception exception = ReadException(exception_channel, ZX_EXCP_THREAD_STARTING,
                                                &aux_start_info);
        EXPECT_EQ(aux_start_info.pid, tu_get_koid(loop.process().get()));
        EXPECT_TRUE(ExceptionHasThread(exception, aux_start_info.tid));
        EXPECT_TRUE(ExceptionHasProcess(exception, aux_start_info.pid));
    }

    // We don't have access to the primary thread handle so just check the aux
    // thread TID to make sure it's correct.
    loop.Step3ReadAuxThreadHandle();
    EXPECT_EQ(aux_start_info.tid, tu_get_koid(loop.aux_thread().get()));

    loop.Step4ShutdownAuxThread();
    zx_exception_info_t aux_exit_info;
    {
        zx::exception exception = ReadException(exception_channel, ZX_EXCP_THREAD_EXITING,
                                                &aux_exit_info);
        EXPECT_TRUE(ExceptionHasThread(exception, aux_exit_info.tid));
        EXPECT_TRUE(ExceptionHasProcess(exception, aux_exit_info.pid));
        EXPECT_EQ(aux_exit_info.tid, aux_start_info.tid);
        EXPECT_EQ(aux_exit_info.pid, aux_start_info.pid);
    }

    loop.Step5ShutdownMainThread();
    zx_exception_info_t primary_exit_info;
    {
        zx::exception exception = ReadException(exception_channel, ZX_EXCP_THREAD_EXITING,
                                                &primary_exit_info);
        EXPECT_TRUE(ExceptionHasThread(exception, primary_exit_info.tid));
        EXPECT_TRUE(ExceptionHasProcess(exception, primary_exit_info.pid));
        EXPECT_EQ(primary_exit_info.tid, primary_start_info.tid);
        EXPECT_EQ(primary_exit_info.pid, primary_start_info.pid);
    }
}

// Parameterized to run against either the TestLoop job or parent job.
template <auto task_func>
void VerifyProcessLifecycle() {
    zx::channel exception_channel;
    {
        TestLoop loop(TestLoop::Control::kManual);

        ASSERT_OK((loop.*task_func)().create_exception_channel(ZX_EXCEPTION_CHANNEL_DEBUGGER,
                                                               &exception_channel));

        // ZX_EXCP_PROCESS_STARTING shouldn't be sent until step 2 when we
        // actually start the first thread on the process.
        loop.Step1CreateProcess();
        EXPECT_EQ(exception_channel.wait_one(ZX_CHANNEL_READABLE, zx::deadline_after(kTestTimeout),
                                             nullptr),
                  ZX_ERR_TIMED_OUT);

        loop.Step2StartThreads();
        zx_exception_info_t info;
        {
            zx::exception exception = ReadException(exception_channel, ZX_EXCP_PROCESS_STARTING,
                                                    &info);
            EXPECT_EQ(info.pid, tu_get_koid(loop.process().get()));
            EXPECT_TRUE(ExceptionHasThread(exception, info.tid));
            EXPECT_TRUE(ExceptionHasProcess(exception, info.pid));
        }

        loop.Step3ReadAuxThreadHandle();
        loop.Step4ShutdownAuxThread();
        loop.Step5ShutdownMainThread();
    }

    // There is no PROCESS_EXITING exception, make sure the kernel finishes
    // closing the channel without putting anything else in it.
    //
    // Unlike processes, jobs don't automatically die with their last child,
    // so the TestLoop handles must be fully closed at this point to get the
    // PEER_CLOSED signal.
    zx_signals_t signals;
    EXPECT_OK(exception_channel.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), &signals));
    EXPECT_FALSE(signals & ZX_CHANNEL_READABLE);
}

TEST(ExceptionTest, ProcessLifecycleJobChannel) {
    VerifyProcessLifecycle<&TestLoop::job>();
}

TEST(ExceptionTest, ProcessLifecycleParentJobChannel) {
    VerifyProcessLifecycle<&TestLoop::parent_job>();
}

TEST(ExceptionTest, ProcessStartExceptionDoesNotBubbleUp) {
    zx::channel parent_exception_channel;
    zx::channel exception_channel;
    {
        TestLoop loop(TestLoop::Control::kManual);

        ASSERT_OK(loop.parent_job().create_exception_channel(ZX_EXCEPTION_CHANNEL_DEBUGGER,
                                                             &parent_exception_channel));
        ASSERT_OK(loop.job().create_exception_channel(ZX_EXCEPTION_CHANNEL_DEBUGGER,
                                                      &exception_channel));

        loop.Step1CreateProcess();
        loop.Step2StartThreads();
        ReadException(exception_channel, ZX_EXCP_PROCESS_STARTING);

        loop.Step3ReadAuxThreadHandle();
        loop.Step4ShutdownAuxThread();
        loop.Step5ShutdownMainThread();
    }

    // The parent job channel should never have seen anything since synthetic
    // PROCESS_STARTING exceptions do not bubble up the job chain.
    zx_signals_t signals;
    EXPECT_OK(parent_exception_channel.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(),
                                                &signals));
    EXPECT_FALSE(signals & ZX_CHANNEL_READABLE);
}

// Lifecycle exceptions should not be seen by normal (non-debug) handlers.
TEST(ExceptionTest, LifecycleExceptionsToDebugHandlersOnly) {
    zx::channel exception_channels[4];
    {
        TestLoop loop(TestLoop::Control::kManual);
        ASSERT_OK(loop.parent_job().create_exception_channel(0, &exception_channels[0]));
        ASSERT_OK(loop.job().create_exception_channel(0, &exception_channels[1]));

        loop.Step1CreateProcess();
        ASSERT_OK(loop.process().create_exception_channel(0, &exception_channels[2]));

        loop.Step2StartThreads();
        loop.Step3ReadAuxThreadHandle();
        ASSERT_OK(loop.aux_thread().create_exception_channel(0, &exception_channels[3]));

        loop.Step4ShutdownAuxThread();
        loop.Step5ShutdownMainThread();
    }

    // None of the normal handlers should have seen any exceptions.
    for (const zx::channel& channel : exception_channels) {
        zx_signals_t signals;
        EXPECT_OK(channel.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), &signals));
        EXPECT_FALSE(signals & ZX_CHANNEL_READABLE);
    }
}

// Returns the state of the thread underlying the given exception or
// an invalid state on failure.
zx_thread_state_t GetExceptionThreadState(const zx::exception& exception) {
    zx::thread thread;
    if (exception.get_thread(&thread) != ZX_OK) {
        return ~0;
    }
    return tu_thread_get_info(thread.get()).state;
}

// A lifecycle exception blocks due to:
//   * process/thread start
//   * thread killing itself via zx_thread_exit()
//
// It does not block due to:
//   * zx_task_kill() on the thread or any of its parents
//
// In the non-blocking case, the exception is still sent, but the thread
// doesn't wait for a response.
TEST(ExceptionTest, LifecycleBlocking) {
    TestLoop loop(TestLoop::Control::kManual);
    loop.Step1CreateProcess();

    zx::channel job_channel;
    ASSERT_OK(loop.job().create_exception_channel(ZX_EXCEPTION_CHANNEL_DEBUGGER, &job_channel));
    zx::channel process_channel;
    ASSERT_OK(loop.process().create_exception_channel(ZX_EXCEPTION_CHANNEL_DEBUGGER,
                                                      &process_channel));

    // Process/thread start: exception handler should block the task.
    loop.Step2StartThreads();
    {
        zx::exception exception = ReadException(job_channel, ZX_EXCP_PROCESS_STARTING);
        zx::nanosleep(zx::deadline_after(kTestTimeout));
        EXPECT_EQ(GetExceptionThreadState(exception), ZX_THREAD_STATE_BLOCKED_EXCEPTION);
    }
    for (int i = 0; i < 2; ++i) {
        zx::exception exception = ReadException(process_channel, ZX_EXCP_THREAD_STARTING);
        zx::nanosleep(zx::deadline_after(kTestTimeout));
        EXPECT_EQ(GetExceptionThreadState(exception), ZX_THREAD_STATE_BLOCKED_EXCEPTION);
    }

    // The aux thread exits gracefully via zx_thread_exit() so should block.
    loop.Step3ReadAuxThreadHandle();
    loop.Step4ShutdownAuxThread();
    {
        zx::exception exception = ReadException(process_channel, ZX_EXCP_THREAD_EXITING);
        zx::nanosleep(zx::deadline_after(kTestTimeout));
        // The thread reports DYING because it takes precedence over BLOCKED,
        // but if it wasn't actually blocking it would report DEAD by now.
        EXPECT_EQ(GetExceptionThreadState(exception), ZX_THREAD_STATE_DYING);
    }

    // The main thread shuts down the whole process via zx_task_kill() so
    // should not block.
    loop.Step5ShutdownMainThread();
    {
        zx::exception exception = ReadException(process_channel, ZX_EXCP_THREAD_EXITING);
        zx::thread thread;
        EXPECT_OK(zx_exception_get_thread(exception.get(), thread.reset_and_get_address()));
        EXPECT_OK(thread.wait_one(ZX_THREAD_TERMINATED, zx::time::infinite(), nullptr));
        EXPECT_EQ(GetExceptionThreadState(exception), ZX_THREAD_STATE_DEAD);
    }
}

// Test read/write register state during (non-synthetic) exceptions.
//
// |task_func|: TestLoop member function to get the task.
// |create_flags|: flags to pass to zx_task_create_exception_channel().
template <auto task_func>
void ReadWriteThreadState(uint32_t create_flags) {
    TestLoop loop;
    zx::channel exception_channel;
    ASSERT_OK((loop.*task_func)().create_exception_channel(create_flags, &exception_channel));

    loop.CrashAuxThread();
    zx::exception exception = ReadException(exception_channel, ZX_EXCP_FATAL_PAGE_FAULT);

    zx_thread_state_general_regs_t regs;
    EXPECT_OK(loop.aux_thread().read_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)));
    EXPECT_OK(loop.aux_thread().write_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)));

    EXPECT_OK(loop.aux_thread().kill());
}

TEST(ExceptionTest, ReadWriteThreadStateFromThreadChannel) {
    ReadWriteThreadState<&TestLoop::aux_thread>(0);
}

TEST(ExceptionTest, ReadWriteThreadStateFromProcessChannel) {
    ReadWriteThreadState<&TestLoop::process>(0);
}

TEST(ExceptionTest, ReadWriteThreadStateFromProcessDebugChannel) {
    ReadWriteThreadState<&TestLoop::process>(ZX_EXCEPTION_CHANNEL_DEBUGGER);
}

TEST(ExceptionTest, ReadWriteThreadStateFromJobChannel) {
    ReadWriteThreadState<&TestLoop::job>(0);
}

TEST(ExceptionTest, ReadWriteThreadStateFromParentJobChannel) {
    ReadWriteThreadState<&TestLoop::parent_job>(0);
}

// Processes an exception and returns the result of trying to read/write
// the thread general registers.
//
// If read/write return different status, marks a test failure and returns
// ZX_ERR_INTERNAL.
zx_status_t ExceptionRegAccess(const zx::channel& channel, zx_excp_type_t type) {
    zx_exception_info_t info;
    zx::exception exception = ReadException(channel, type, &info);

    zx::thread thread;
    EXPECT_OK(exception.get_thread(&thread));
    if (!thread.is_valid()) {
        return ZX_ERR_INTERNAL;
    }

    zx_thread_state_general_regs_t regs;
    zx_status_t read_status = thread.read_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs));
    zx_status_t write_status = thread.write_state(ZX_THREAD_STATE_GENERAL_REGS, &regs,
                                                  sizeof(regs));

    EXPECT_EQ(read_status, write_status);
    if (read_status != write_status) {
        return ZX_ERR_INTERNAL;
    }
    return read_status;
}

// Read/write register state is supported during STARTING exceptions, but not
// during EXITING.
TEST(ExceptionTest, SyntheticExceptionReadWriteRegs) {
    zx::channel job_channel;
    zx::channel process_channel;

    TestLoop loop(TestLoop::Control::kManual);
    ASSERT_OK(loop.job().create_exception_channel(ZX_EXCEPTION_CHANNEL_DEBUGGER, &job_channel));

    loop.Step1CreateProcess();
    ASSERT_OK(loop.process().create_exception_channel(ZX_EXCEPTION_CHANNEL_DEBUGGER,
                                                      &process_channel));

    loop.Step2StartThreads();
    EXPECT_OK(ExceptionRegAccess(job_channel, ZX_EXCP_PROCESS_STARTING));
    EXPECT_OK(ExceptionRegAccess(process_channel, ZX_EXCP_THREAD_STARTING));
    EXPECT_OK(ExceptionRegAccess(process_channel, ZX_EXCP_THREAD_STARTING));

    loop.Step3ReadAuxThreadHandle();
    loop.Step4ShutdownAuxThread();
    EXPECT_EQ(ExceptionRegAccess(process_channel, ZX_EXCP_THREAD_EXITING),
              ZX_ERR_NOT_SUPPORTED);

    // When the main thread is shut down it kills the whole process, which
    // causes it to stop waiting for responses from exception handlers. We'll
    // still receive the exception, but by the time we process it here it's
    // likely that the thread is already dead so we can't check reg access.
    loop.Step5ShutdownMainThread();
    ReadException(process_channel, ZX_EXCP_THREAD_EXITING);
}

void CrashThreadFunc(uintptr_t, uintptr_t) {
    crash_me();
    zx_thread_exit();
}

// Test killing a thread then immediately closing the exception never
// propagates the exception (ZX-4105).
//
// This isn't possible to test deterministically so we just try to run it
// for a little bit, if this looks like it's becoming flaky it probably
// indicates a real underlying bug. Failures would manifest as the unittest
// crash handler seeing an unregistered crash.
constexpr zx::duration kRaceTestDuration = zx::sec(1);

TEST(ExceptionTest, KillThreadAndClosePortRace) {
    zx::time end_time = zx::deadline_after(kRaceTestDuration);
    while (zx::clock::get_monotonic() < end_time) {
        zx::thread thread;
        ASSERT_OK(zx::thread::create(*zx::process::self(), "crasher", strlen("crasher"),
                                     0u, &thread));

        zx::port port;
        ASSERT_OK(zx::port::create(0, &port));
        ASSERT_OK(thread.bind_exception_port(port, 0, 0));

        alignas(16) static uint8_t thread_stack[128];
        thread.start(&CrashThreadFunc, thread_stack + sizeof(thread_stack), 0, 0);

        zx_port_packet_t packet;
        ASSERT_OK(port.wait(zx::time::infinite(), &packet));
        ASSERT_TRUE(ZX_PKT_IS_EXCEPTION(packet.type));

        ASSERT_OK(thread.kill());
        port.reset();
        ASSERT_OK(thread.wait_one(ZX_THREAD_TERMINATED, zx::time::infinite(), nullptr));
    }
}

TEST(ExceptionTest, KillThreadAndCloseExceptionRace) {
    zx::time end_time = zx::deadline_after(kRaceTestDuration);
    while (zx::clock::get_monotonic() < end_time) {
        zx::thread thread;
        ASSERT_OK(zx::thread::create(*zx::process::self(), "crasher", strlen("crasher"),
                                     0u, &thread));

        zx::channel channel;
        ASSERT_OK(thread.create_exception_channel(0, &channel));

        alignas(16) static uint8_t thread_stack[128];
        thread.start(&CrashThreadFunc, thread_stack + sizeof(thread_stack), 0, 0);

        zx::exception exception = ReadException(channel, ZX_EXCP_FATAL_PAGE_FAULT);
        ASSERT_TRUE(exception.is_valid());

        ASSERT_OK(thread.kill());
        exception.reset();
        ASSERT_OK(thread.wait_one(ZX_THREAD_TERMINATED, zx::time::infinite(), nullptr));
    }
}

} // namespace

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

    // We use this same binary for both the main test runner and a test process
    // running msg_loop(), but this can interfere with any common zxtest
    // arguments that get passed. If this becomes a problem, consider using
    // mini-process as the test process instead.
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

    return RUN_ALL_TESTS(argc, argv);
}
