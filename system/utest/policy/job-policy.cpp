// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdio.h>
#include <threads.h>

#include <mx/channel.h>
#include <mx/event.h>
#include <mx/handle.h>
#include <mx/job.h>
#include <mx/port.h>
#include <mx/process.h>
#include <mx/thread.h>
#include <mx/vmar.h>

#include <fbl/algorithm.h>
#include <fbl/type_support.h>

#include <magenta/process.h>
#include <magenta/syscalls/debug.h>
#include <magenta/syscalls/exception.h>
#include <magenta/syscalls/policy.h>
#include <magenta/syscalls/port.h>

#include <mini-process/mini-process.h>

#include <unistd.h>
#include <unittest/unittest.h>

static const unsigned kExceptionPortKey = 42u;

// Basic job operation is tested by core-tests.
static mx::job make_job() {
    mx::job job;
    if (mx::job::create(mx_job_default(), 0u, &job) != MX_OK)
        return mx::job();
    return job;
}

static mx::process make_test_process(const mx::job& job, mx::thread* out_thread,
                                     mx_handle_t* ctrl) {
    mx::vmar vmar;
    mx::process proc;
    mx_status_t status = mx::process::create(job, "poltst", 6u, 0u, &proc, &vmar);
    if (status != MX_OK)
        return mx::process();

    mx::thread thread;
    status = mx::thread::create(proc, "poltst", 6u, 0, &thread);
    if (status != MX_OK)
        return mx::process();
    if (out_thread) {
        status = thread.duplicate(MX_RIGHT_SAME_RIGHTS, out_thread);
        if (status != MX_OK)
            return mx::process();
    }

    mx::event event;
    status = mx::event::create(0u, &event);
    if (status != MX_OK)
        return mx::process();

    auto thr = thread.release();
    status = start_mini_process_etc(proc.get(), thr, vmar.get(), event.release(), ctrl);
    if (status != MX_OK)
        return mx::process();

    return proc;
}

static bool abs_then_rel() {
    BEGIN_TEST;

    mx_policy_basic_t policy[] = {
        { MX_POL_BAD_HANDLE, MX_POL_ACTION_KILL } };

    auto job = make_job();
    EXPECT_EQ(job.set_policy(
        MX_JOB_POL_ABSOLUTE, MX_JOB_POL_BASIC, policy, fbl::count_of(policy)), MX_OK);

    // A contradictory policy should fail.
    policy[0].policy = MX_POL_ACTION_EXCEPTION | MX_POL_ACTION_DENY;
    EXPECT_EQ(job.set_policy(
        MX_JOB_POL_ABSOLUTE, MX_JOB_POL_BASIC, policy, fbl::count_of(policy)), MX_ERR_ALREADY_EXISTS);

    // The same again will succeed.
    policy[0].policy = MX_POL_ACTION_KILL;
    EXPECT_EQ(job.set_policy(
        MX_JOB_POL_ABSOLUTE, MX_JOB_POL_BASIC, policy, fbl::count_of(policy)), MX_OK);

    // A contradictory relative policy will succeed, but is a no-op
    policy[0].policy = MX_POL_ACTION_ALLOW;
    EXPECT_EQ(job.set_policy(
        MX_JOB_POL_RELATIVE, MX_JOB_POL_BASIC, policy, fbl::count_of(policy)), MX_OK);

    mx_policy_basic_t more[] = {
        { MX_POL_NEW_CHANNEL, MX_POL_ACTION_ALLOW | MX_POL_ACTION_EXCEPTION },
        { MX_POL_NEW_FIFO, MX_POL_ACTION_DENY } };

    // An additional absolute policy that doesn't contradict existing
    // policy can be added.
    EXPECT_EQ(job.set_policy(
        MX_JOB_POL_ABSOLUTE, MX_JOB_POL_BASIC, more, fbl::count_of(more)), MX_OK);

    END_TEST;
}

static bool invalid_calls(uint32_t options) {
    auto job = make_job();

    EXPECT_EQ(job.set_policy(options, MX_JOB_POL_BASIC, nullptr, 0u), MX_ERR_INVALID_ARGS);

    EXPECT_EQ(job.set_policy(options, MX_JOB_POL_BASIC, nullptr, 5u), MX_ERR_INVALID_ARGS);

    mx_policy_basic_t policy1[] = {
        { MX_POL_BAD_HANDLE, MX_POL_ACTION_KILL },
    };

    EXPECT_EQ(job.set_policy(options, MX_JOB_POL_BASIC, policy1, 0u), MX_ERR_INVALID_ARGS);

    mx_policy_basic_t policy2[] = {
        { 100001u, MX_POL_ACTION_KILL },
    };

    EXPECT_EQ(job.set_policy(
        options, MX_JOB_POL_BASIC, policy2, fbl::count_of(policy2)), MX_ERR_INVALID_ARGS);

    mx_policy_basic_t policy3[] = {
        { MX_POL_BAD_HANDLE, 100001u },
    };

    EXPECT_EQ(job.set_policy(
        options, MX_JOB_POL_BASIC, policy3, fbl::count_of(policy2)), MX_ERR_NOT_SUPPORTED);

    // The job will still accept a valid combination:
    mx_policy_basic_t policy4[] = {
        { MX_POL_BAD_HANDLE, MX_POL_ACTION_KILL } };

    EXPECT_EQ(job.set_policy(
        options, MX_JOB_POL_BASIC, policy4, fbl::count_of(policy4)), MX_OK);

    return true;
}

static bool invalid_calls_abs() {
    BEGIN_TEST;

    invalid_calls(MX_JOB_POL_ABSOLUTE);

    END_TEST;
}

static bool invalid_calls_rel() {
    BEGIN_TEST;

    invalid_calls(MX_JOB_POL_RELATIVE);

    END_TEST;
}

// Test that executing the given mini-process.h command (|minip_cmd|)
// produces the given result (|expect|) when the given policy is in force.
static bool test_invoking_policy(
    mx_policy_basic_t* pol, uint32_t pol_count, uint32_t minip_cmd, mx_status_t expect) {
    auto job = make_job();
    ASSERT_EQ(job.set_policy(MX_JOB_POL_ABSOLUTE, MX_JOB_POL_BASIC, pol, pol_count), MX_OK);

    mx_handle_t ctrl;
    auto proc = make_test_process(job, nullptr, &ctrl);
    ASSERT_TRUE(proc.is_valid());
    ASSERT_NE(ctrl, MX_HANDLE_INVALID);

    mx_handle_t obj;
    EXPECT_EQ(mini_process_cmd(ctrl, minip_cmd, &obj), expect);
    EXPECT_EQ(mini_process_cmd(ctrl, MINIP_CMD_EXIT_NORMAL, nullptr), MX_ERR_PEER_CLOSED);

    mx_handle_close(ctrl);
    return true;
}

static bool enforce_deny_event() {
    BEGIN_TEST;

    mx_policy_basic_t policy[] = { { MX_POL_NEW_EVENT, MX_POL_ACTION_DENY } };
    test_invoking_policy(policy, fbl::count_of(policy), MINIP_CMD_CREATE_EVENT,
                         MX_ERR_ACCESS_DENIED);

    END_TEST;
}

static bool enforce_deny_channel() {
    BEGIN_TEST;

    mx_policy_basic_t policy[] = { { MX_POL_NEW_CHANNEL, MX_POL_ACTION_DENY } };
    test_invoking_policy(policy, fbl::count_of(policy), MINIP_CMD_CREATE_CHANNEL,
                         MX_ERR_ACCESS_DENIED);

    END_TEST;
}

static bool enforce_deny_any() {
    BEGIN_TEST;

    mx_policy_basic_t policy[] = { { MX_POL_NEW_ANY, MX_POL_ACTION_DENY } };
    test_invoking_policy(policy, fbl::count_of(policy), MINIP_CMD_CREATE_EVENT,
                         MX_ERR_ACCESS_DENIED);

    END_TEST;
}

static bool enforce_allow_any() {
    BEGIN_TEST;

    mx_policy_basic_t policy[] = { { MX_POL_NEW_ANY, MX_POL_ACTION_ALLOW } };
    test_invoking_policy(policy, fbl::count_of(policy), MINIP_CMD_CREATE_EVENT,
                         MX_OK);

    END_TEST;
}

static bool enforce_deny_but_event() {
    BEGIN_TEST;

    mx_policy_basic_t policy[] = {
        { MX_POL_NEW_ANY, MX_POL_ACTION_DENY },
        { MX_POL_NEW_EVENT, MX_POL_ACTION_ALLOW }
    };
    test_invoking_policy(policy, fbl::count_of(policy), MINIP_CMD_CREATE_EVENT,
                         MX_OK);
    test_invoking_policy(policy, fbl::count_of(policy), MINIP_CMD_CREATE_CHANNEL,
                         MX_ERR_ACCESS_DENIED);

    END_TEST;
}

static bool get_koid(mx_handle_t handle, mx_koid_t* koid) {
    mx_info_handle_basic_t info;
    ASSERT_EQ(mx_object_get_info(
                  handle, MX_INFO_HANDLE_BASIC, &info, sizeof(info),
                  nullptr, nullptr), MX_OK);
    *koid = info.koid;
    return true;
}

#if defined(__x86_64__)

typedef struct mx_x86_64_general_regs mx_general_regs_t;

static uint64_t get_syscall_result(mx_general_regs_t* regs) {
    return regs->rax;
}

#elif defined(__aarch64__)

typedef struct mx_arm64_general_regs mx_general_regs_t;

static uint64_t get_syscall_result(mx_general_regs_t* regs) {
    return regs->r[0];
}

#else
# error Unsupported architecture
#endif

// Like test_invoking_policy(), this tests that executing the given
// mini-process.h command produces the given result when the given policy
// is in force.  In addition, it tests that a debug port exception gets
// generated.
static bool test_invoking_policy_with_exception(
    mx_policy_basic_t* policy, uint32_t policy_count, uint32_t minip_cmd,
    mx_status_t expected_syscall_result) {
    auto job = make_job();
    ASSERT_EQ(job.set_policy(MX_JOB_POL_ABSOLUTE, MX_JOB_POL_BASIC, policy,
                             policy_count), MX_OK);

    mx_handle_t ctrl;
    mx::thread thread;
    auto proc = make_test_process(job, &thread, &ctrl);
    ASSERT_TRUE(proc.is_valid());
    ASSERT_NE(ctrl, MX_HANDLE_INVALID);

    mx_handle_t exc_port;
    ASSERT_EQ(mx_port_create(0, &exc_port), MX_OK);
    ASSERT_EQ(mx_task_bind_exception_port(
                  proc.get(), exc_port, kExceptionPortKey,
                  MX_EXCEPTION_PORT_DEBUGGER),
              MX_OK);

    EXPECT_EQ(mini_process_cmd_send(ctrl, minip_cmd), MX_OK);

    // Check that the subprocess did not return a reply yet (indicating
    // that it was suspended).
    EXPECT_EQ(mx_object_wait_one(ctrl, MX_CHANNEL_READABLE,
                                 mx_deadline_after(MX_MSEC(1)), nullptr),
              MX_ERR_TIMED_OUT);

    // Check that we receive an exception message.
    mx_port_packet_t packet;
    ASSERT_EQ(mx_port_wait(exc_port, MX_TIME_INFINITE, &packet, 0), MX_OK);

    // Check the exception message contents.
    ASSERT_EQ(packet.key, kExceptionPortKey);
    ASSERT_EQ(packet.type, (uint32_t)MX_EXCP_POLICY_ERROR);

    mx_koid_t pid;
    mx_koid_t tid;
    ASSERT_TRUE(get_koid(proc.get(), &pid));
    ASSERT_TRUE(get_koid(thread.get(), &tid));
    ASSERT_EQ(packet.exception.pid, pid);
    ASSERT_EQ(packet.exception.tid, tid);

    // Check that we can read the thread's register state.
    mx_general_regs_t regs;
    uint32_t size_read;
    ASSERT_EQ(mx_thread_read_state(thread.get(), MX_THREAD_STATE_REGSET0,
                                   &regs, sizeof(regs), &size_read),
              MX_OK);
    ASSERT_EQ(size_read, sizeof(regs));
    ASSERT_EQ(get_syscall_result(&regs), (uint64_t)expected_syscall_result);
    // TODO(mseaborn): Check the values of other registers.  We could check
    // that rip/pc is within the VDSO, which will require figuring out
    // where the VDSO is mapped.  We could check that unwinding the stack
    // using crashlogger gives a correct backtrace.

    // Resume the thread.
    ASSERT_EQ(mx_task_resume(thread.get(), MX_RESUME_EXCEPTION), MX_OK);
    // Check that the read-ready state of the channel changed compared with
    // the earlier check.
    EXPECT_EQ(mx_object_wait_one(ctrl, MX_CHANNEL_READABLE, MX_TIME_INFINITE,
                                 nullptr),
              MX_OK);

    // Check that we receive a reply message from the resumed thread.
    mx_handle_t obj;
    EXPECT_EQ(mini_process_cmd_read_reply(ctrl, &obj),
              expected_syscall_result);
    if (expected_syscall_result == MX_OK)
        EXPECT_EQ(mx_handle_close(obj), MX_OK);

    // Clean up: Tell the subprocess to exit.
    EXPECT_EQ(mini_process_cmd(ctrl, MINIP_CMD_EXIT_NORMAL, nullptr),
              MX_ERR_PEER_CLOSED);

    mx_handle_close(ctrl);

    return true;
}

static bool test_exception_on_new_event_and_deny() {
    BEGIN_TEST;

    mx_policy_basic_t policy[] = {
        { MX_POL_NEW_EVENT, MX_POL_ACTION_DENY | MX_POL_ACTION_EXCEPTION },
    };
    test_invoking_policy_with_exception(
        policy, fbl::count_of(policy), MINIP_CMD_CREATE_EVENT, MX_ERR_ACCESS_DENIED);

    END_TEST;
}

static bool test_exception_on_new_event_but_allow() {
    BEGIN_TEST;

    mx_policy_basic_t policy[] = {
        { MX_POL_NEW_EVENT, MX_POL_ACTION_ALLOW | MX_POL_ACTION_EXCEPTION },
    };
    test_invoking_policy_with_exception(
        policy, fbl::count_of(policy), MINIP_CMD_CREATE_EVENT, MX_OK);

    END_TEST;
}

// Test MX_POL_BAD_HANDLE when syscalls are allowed to continue.
static bool test_error_on_bad_handle() {
    BEGIN_TEST;

    // The ALLOW and DENY actions should be equivalent for MX_POL_BAD_HANDLE.
    uint32_t actions[] = { MX_POL_ACTION_ALLOW, MX_POL_ACTION_DENY };
    for (uint32_t action : actions) {
        unittest_printf_critical("Testing action=%d\n", action);
        mx_policy_basic_t policy[] = {
            { MX_POL_BAD_HANDLE, action },
        };
        test_invoking_policy(
            policy, fbl::count_of(policy), MINIP_CMD_USE_BAD_HANDLE_CLOSED,
            MX_ERR_BAD_HANDLE);
        test_invoking_policy(
            policy, fbl::count_of(policy), MINIP_CMD_USE_BAD_HANDLE_TRANSFERRED,
            MX_ERR_BAD_HANDLE);
    }

    END_TEST;
}

// Test MX_POL_BAD_HANDLE with MX_POL_ACTION_EXCEPTION.
static bool test_exception_on_bad_handle() {
    BEGIN_TEST;

    // The ALLOW and DENY actions should be equivalent for MX_POL_BAD_HANDLE.
    uint32_t actions[] = { MX_POL_ACTION_ALLOW, MX_POL_ACTION_DENY };
    for (uint32_t action : actions) {
        unittest_printf_critical("Testing action=%d\n", action);
        mx_policy_basic_t policy[] = {
            { MX_POL_BAD_HANDLE, action | MX_POL_ACTION_EXCEPTION },
        };
        test_invoking_policy_with_exception(
            policy, fbl::count_of(policy), MINIP_CMD_USE_BAD_HANDLE_CLOSED,
            MX_ERR_BAD_HANDLE);
        test_invoking_policy_with_exception(
            policy, fbl::count_of(policy), MINIP_CMD_USE_BAD_HANDLE_TRANSFERRED,
            MX_ERR_BAD_HANDLE);
    }

    END_TEST;
}

BEGIN_TEST_CASE(job_policy)
RUN_TEST(invalid_calls_abs)
RUN_TEST(invalid_calls_rel)
RUN_TEST(abs_then_rel)
RUN_TEST(enforce_deny_event)
RUN_TEST(enforce_deny_channel)
RUN_TEST(enforce_deny_any)
RUN_TEST(enforce_allow_any)
RUN_TEST(enforce_deny_but_event)
RUN_TEST(test_exception_on_new_event_and_deny)
RUN_TEST(test_exception_on_new_event_but_allow)
RUN_TEST(test_error_on_bad_handle)
RUN_TEST(test_exception_on_bad_handle)
END_TEST_CASE(job_policy)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
