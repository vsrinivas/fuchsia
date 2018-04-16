// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdio.h>
#include <threads.h>

#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/handle.h>
#include <lib/zx/job.h>
#include <lib/zx/port.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <lib/zx/vmar.h>

#include <fbl/algorithm.h>
#include <fbl/type_support.h>

#include <zircon/process.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/policy.h>
#include <zircon/syscalls/port.h>

#include <mini-process/mini-process.h>

#include <unistd.h>
#include <unittest/unittest.h>

static const unsigned kExceptionPortKey = 42u;

// Basic job operation is tested by core-tests.
static zx::job make_job() {
    zx::job job;
    if (zx::job::create(zx_job_default(), 0u, &job) != ZX_OK)
        return zx::job();
    return job;
}

static zx::process make_test_process(const zx::job& job, zx::thread* out_thread,
                                     zx_handle_t* ctrl) {
    zx::vmar vmar;
    zx::process proc;
    zx_status_t status = zx::process::create(job, "poltst", 6u, 0u, &proc, &vmar);
    if (status != ZX_OK)
        return zx::process();

    zx::thread thread;
    status = zx::thread::create(proc, "poltst", 6u, 0, &thread);
    if (status != ZX_OK)
        return zx::process();
    if (out_thread) {
        status = thread.duplicate(ZX_RIGHT_SAME_RIGHTS, out_thread);
        if (status != ZX_OK)
            return zx::process();
    }

    zx::event event;
    status = zx::event::create(0u, &event);
    if (status != ZX_OK)
        return zx::process();

    auto thr = thread.release();
    status = start_mini_process_etc(proc.get(), thr, vmar.get(), event.release(), ctrl);
    if (status != ZX_OK)
        return zx::process();

    return proc;
}

static bool abs_then_rel() {
    BEGIN_TEST;

    zx_policy_basic_t policy[] = {
        { ZX_POL_BAD_HANDLE, ZX_POL_ACTION_KILL } };

    auto job = make_job();
    EXPECT_EQ(job.set_policy(
        ZX_JOB_POL_ABSOLUTE, ZX_JOB_POL_BASIC, policy, static_cast<uint32_t>(fbl::count_of(policy))), ZX_OK);

    // A contradictory policy should fail.
    policy[0].policy = ZX_POL_ACTION_EXCEPTION | ZX_POL_ACTION_DENY;
    EXPECT_EQ(job.set_policy(
        ZX_JOB_POL_ABSOLUTE, ZX_JOB_POL_BASIC, policy, static_cast<uint32_t>(fbl::count_of(policy))), ZX_ERR_ALREADY_EXISTS);

    // The same again will succeed.
    policy[0].policy = ZX_POL_ACTION_KILL;
    EXPECT_EQ(job.set_policy(
        ZX_JOB_POL_ABSOLUTE, ZX_JOB_POL_BASIC, policy, static_cast<uint32_t>(fbl::count_of(policy))), ZX_OK);

    // A contradictory relative policy will succeed, but is a no-op
    policy[0].policy = ZX_POL_ACTION_ALLOW;
    EXPECT_EQ(job.set_policy(
        ZX_JOB_POL_RELATIVE, ZX_JOB_POL_BASIC, policy, static_cast<uint32_t>(fbl::count_of(policy))), ZX_OK);

    zx_policy_basic_t more[] = {
        { ZX_POL_NEW_CHANNEL, ZX_POL_ACTION_ALLOW | ZX_POL_ACTION_EXCEPTION },
        { ZX_POL_NEW_FIFO, ZX_POL_ACTION_DENY } };

    // An additional absolute policy that doesn't contradict existing
    // policy can be added.
    EXPECT_EQ(job.set_policy(
        ZX_JOB_POL_ABSOLUTE, ZX_JOB_POL_BASIC, more, static_cast<uint32_t>(fbl::count_of(more))), ZX_OK);

    END_TEST;
}

static bool invalid_calls(uint32_t options) {
    auto job = make_job();

    EXPECT_EQ(job.set_policy(options, ZX_JOB_POL_BASIC, nullptr, 0u), ZX_ERR_INVALID_ARGS);

    EXPECT_EQ(job.set_policy(options, ZX_JOB_POL_BASIC, nullptr, 5u), ZX_ERR_INVALID_ARGS);

    zx_policy_basic_t policy1[] = {
        { ZX_POL_BAD_HANDLE, ZX_POL_ACTION_KILL },
    };

    EXPECT_EQ(job.set_policy(options, ZX_JOB_POL_BASIC, policy1, 0u), ZX_ERR_INVALID_ARGS);

    zx_policy_basic_t policy2[] = {
        { 100001u, ZX_POL_ACTION_KILL },
    };

    EXPECT_EQ(job.set_policy(
        options, ZX_JOB_POL_BASIC, policy2, static_cast<uint32_t>(fbl::count_of(policy2))), ZX_ERR_INVALID_ARGS);

    zx_policy_basic_t policy3[] = {
        { ZX_POL_BAD_HANDLE, 100001u },
    };

    EXPECT_EQ(job.set_policy(
        options, ZX_JOB_POL_BASIC, policy3, static_cast<uint32_t>(fbl::count_of(policy2))), ZX_ERR_NOT_SUPPORTED);

    // The job will still accept a valid combination:
    zx_policy_basic_t policy4[] = {
        { ZX_POL_BAD_HANDLE, ZX_POL_ACTION_KILL } };

    EXPECT_EQ(job.set_policy(
        options, ZX_JOB_POL_BASIC, policy4, static_cast<uint32_t>(fbl::count_of(policy4))), ZX_OK);

    return true;
}

static bool invalid_calls_abs() {
    BEGIN_TEST;

    invalid_calls(ZX_JOB_POL_ABSOLUTE);

    END_TEST;
}

static bool invalid_calls_rel() {
    BEGIN_TEST;

    invalid_calls(ZX_JOB_POL_RELATIVE);

    END_TEST;
}

// Test that executing the given mini-process.h command (|minip_cmd|)
// produces the given result (|expect|) when the given policy is in force.
static bool test_invoking_policy(
    zx_policy_basic_t* pol, uint32_t pol_count, uint32_t minip_cmd, zx_status_t expect) {
    auto job = make_job();
    ASSERT_EQ(job.set_policy(ZX_JOB_POL_ABSOLUTE, ZX_JOB_POL_BASIC, pol, pol_count), ZX_OK);

    zx_handle_t ctrl;
    auto proc = make_test_process(job, nullptr, &ctrl);
    ASSERT_TRUE(proc.is_valid());
    ASSERT_NE(ctrl, ZX_HANDLE_INVALID);

    zx_handle_t obj;
    EXPECT_EQ(mini_process_cmd(ctrl, minip_cmd, &obj), expect);
    EXPECT_EQ(mini_process_cmd(ctrl, MINIP_CMD_EXIT_NORMAL, nullptr), ZX_ERR_PEER_CLOSED);

    zx_handle_close(ctrl);
    return true;
}

static bool enforce_deny_event() {
    BEGIN_TEST;

    zx_policy_basic_t policy[] = { { ZX_POL_NEW_EVENT, ZX_POL_ACTION_DENY } };
    test_invoking_policy(policy, static_cast<uint32_t>(fbl::count_of(policy)), MINIP_CMD_CREATE_EVENT,
                         ZX_ERR_ACCESS_DENIED);

    END_TEST;
}

static bool enforce_deny_channel() {
    BEGIN_TEST;

    zx_policy_basic_t policy[] = { { ZX_POL_NEW_CHANNEL, ZX_POL_ACTION_DENY } };
    test_invoking_policy(policy, static_cast<uint32_t>(fbl::count_of(policy)), MINIP_CMD_CREATE_CHANNEL,
                         ZX_ERR_ACCESS_DENIED);

    END_TEST;
}

static bool enforce_deny_any() {
    BEGIN_TEST;

    zx_policy_basic_t policy[] = { { ZX_POL_NEW_ANY, ZX_POL_ACTION_DENY } };
    test_invoking_policy(policy, static_cast<uint32_t>(fbl::count_of(policy)), MINIP_CMD_CREATE_EVENT,
                         ZX_ERR_ACCESS_DENIED);

    END_TEST;
}

static bool enforce_allow_any() {
    BEGIN_TEST;

    zx_policy_basic_t policy[] = { { ZX_POL_NEW_ANY, ZX_POL_ACTION_ALLOW } };
    test_invoking_policy(policy, static_cast<uint32_t>(fbl::count_of(policy)), MINIP_CMD_CREATE_EVENT,
                         ZX_OK);

    END_TEST;
}

static bool enforce_deny_but_event() {
    BEGIN_TEST;

    zx_policy_basic_t policy[] = {
        { ZX_POL_NEW_ANY, ZX_POL_ACTION_DENY },
        { ZX_POL_NEW_EVENT, ZX_POL_ACTION_ALLOW }
    };
    test_invoking_policy(policy, static_cast<uint32_t>(fbl::count_of(policy)), MINIP_CMD_CREATE_EVENT,
                         ZX_OK);
    test_invoking_policy(policy, static_cast<uint32_t>(fbl::count_of(policy)), MINIP_CMD_CREATE_CHANNEL,
                         ZX_ERR_ACCESS_DENIED);

    END_TEST;
}

static bool get_koid(zx_handle_t handle, zx_koid_t* koid) {
    zx_info_handle_basic_t info;
    ASSERT_EQ(zx_object_get_info(
                  handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info),
                  nullptr, nullptr), ZX_OK);
    *koid = info.koid;
    return true;
}

#if defined(__x86_64__)

static uint64_t get_syscall_result(zx_thread_state_general_regs_t* regs) {
    return regs->rax;
}

#elif defined(__aarch64__)

static uint64_t get_syscall_result(zx_thread_state_general_regs_t* regs) {
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
    zx_policy_basic_t* policy, uint32_t policy_count, uint32_t minip_cmd,
    zx_status_t expected_syscall_result) {
    auto job = make_job();
    ASSERT_EQ(job.set_policy(ZX_JOB_POL_ABSOLUTE, ZX_JOB_POL_BASIC, policy,
                             policy_count), ZX_OK);

    zx_handle_t ctrl;
    zx::thread thread;
    auto proc = make_test_process(job, &thread, &ctrl);
    ASSERT_TRUE(proc.is_valid());
    ASSERT_NE(ctrl, ZX_HANDLE_INVALID);

    zx_handle_t exc_port;
    ASSERT_EQ(zx_port_create(0, &exc_port), ZX_OK);
    ASSERT_EQ(zx_task_bind_exception_port(
                  proc.get(), exc_port, kExceptionPortKey,
                  ZX_EXCEPTION_PORT_DEBUGGER),
              ZX_OK);

    EXPECT_EQ(mini_process_cmd_send(ctrl, minip_cmd), ZX_OK);

    // Check that the subprocess did not return a reply yet (indicating
    // that it was suspended).
    EXPECT_EQ(zx_object_wait_one(ctrl, ZX_CHANNEL_READABLE,
                                 zx_deadline_after(ZX_MSEC(1)), nullptr),
              ZX_ERR_TIMED_OUT);

    // Check that we receive an exception message.
    zx_port_packet_t packet;
    ASSERT_EQ(zx_port_wait(exc_port, ZX_TIME_INFINITE, &packet, 1), ZX_OK);

    // Check the exception message contents.
    ASSERT_EQ(packet.key, kExceptionPortKey);
    ASSERT_EQ(packet.type, (uint32_t)ZX_EXCP_POLICY_ERROR);

    zx_koid_t pid;
    zx_koid_t tid;
    ASSERT_TRUE(get_koid(proc.get(), &pid));
    ASSERT_TRUE(get_koid(thread.get(), &tid));
    ASSERT_EQ(packet.exception.pid, pid);
    ASSERT_EQ(packet.exception.tid, tid);

    // Check that we can read the thread's register state.
    zx_thread_state_general_regs_t regs;
    ASSERT_EQ(zx_thread_read_state(thread.get(), ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)),
              ZX_OK);
    ASSERT_EQ(get_syscall_result(&regs), (uint64_t)expected_syscall_result);
    // TODO(mseaborn): Check the values of other registers.  We could check
    // that rip/pc is within the VDSO, which will require figuring out
    // where the VDSO is mapped.  We could check that unwinding the stack
    // using crashlogger gives a correct backtrace.

    // Resume the thread.
    ASSERT_EQ(zx_task_resume(thread.get(), ZX_RESUME_EXCEPTION), ZX_OK);
    // Check that the read-ready state of the channel changed compared with
    // the earlier check.
    EXPECT_EQ(zx_object_wait_one(ctrl, ZX_CHANNEL_READABLE, ZX_TIME_INFINITE,
                                 nullptr),
              ZX_OK);

    // Check that we receive a reply message from the resumed thread.
    zx_handle_t obj;
    EXPECT_EQ(mini_process_cmd_read_reply(ctrl, &obj),
              expected_syscall_result);
    if (expected_syscall_result == ZX_OK)
        EXPECT_EQ(zx_handle_close(obj), ZX_OK);

    // Clean up: Tell the subprocess to exit.
    EXPECT_EQ(mini_process_cmd(ctrl, MINIP_CMD_EXIT_NORMAL, nullptr),
              ZX_ERR_PEER_CLOSED);

    zx_handle_close(ctrl);

    return true;
}

static bool test_exception_on_new_event_and_deny() {
    BEGIN_TEST;

    zx_policy_basic_t policy[] = {
        { ZX_POL_NEW_EVENT, ZX_POL_ACTION_DENY | ZX_POL_ACTION_EXCEPTION },
    };
    test_invoking_policy_with_exception(
        policy, static_cast<uint32_t>(fbl::count_of(policy)), MINIP_CMD_CREATE_EVENT, ZX_ERR_ACCESS_DENIED);

    END_TEST;
}

static bool test_exception_on_new_event_but_allow() {
    BEGIN_TEST;

    zx_policy_basic_t policy[] = {
        { ZX_POL_NEW_EVENT, ZX_POL_ACTION_ALLOW | ZX_POL_ACTION_EXCEPTION },
    };
    test_invoking_policy_with_exception(
        policy, static_cast<uint32_t>(fbl::count_of(policy)), MINIP_CMD_CREATE_EVENT, ZX_OK);

    END_TEST;
}

// Test ZX_POL_BAD_HANDLE when syscalls are allowed to continue.
static bool test_error_on_bad_handle() {
    BEGIN_TEST;

    // The ALLOW and DENY actions should be equivalent for ZX_POL_BAD_HANDLE.
    uint32_t actions[] = { ZX_POL_ACTION_ALLOW, ZX_POL_ACTION_DENY };
    for (uint32_t action : actions) {
        unittest_printf_critical("Testing action=%d\n", action);
        zx_policy_basic_t policy[] = {
            { ZX_POL_BAD_HANDLE, action },
        };
        test_invoking_policy(
            policy, static_cast<uint32_t>(fbl::count_of(policy)), MINIP_CMD_USE_BAD_HANDLE_CLOSED,
            ZX_ERR_BAD_HANDLE);
        test_invoking_policy(
            policy, static_cast<uint32_t>(fbl::count_of(policy)), MINIP_CMD_USE_BAD_HANDLE_TRANSFERRED,
            ZX_ERR_BAD_HANDLE);
    }

    END_TEST;
}

// Test ZX_POL_BAD_HANDLE with ZX_POL_ACTION_EXCEPTION.
static bool test_exception_on_bad_handle() {
    BEGIN_TEST;

    // The ALLOW and DENY actions should be equivalent for ZX_POL_BAD_HANDLE.
    uint32_t actions[] = { ZX_POL_ACTION_ALLOW, ZX_POL_ACTION_DENY };
    for (uint32_t action : actions) {
        unittest_printf_critical("Testing action=%d\n", action);
        zx_policy_basic_t policy[] = {
            { ZX_POL_BAD_HANDLE, action | ZX_POL_ACTION_EXCEPTION },
        };
        test_invoking_policy_with_exception(
            policy, static_cast<uint32_t>(fbl::count_of(policy)), MINIP_CMD_USE_BAD_HANDLE_CLOSED,
            ZX_ERR_BAD_HANDLE);
        test_invoking_policy_with_exception(
            policy, static_cast<uint32_t>(fbl::count_of(policy)), MINIP_CMD_USE_BAD_HANDLE_TRANSFERRED,
            ZX_ERR_BAD_HANDLE);
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
