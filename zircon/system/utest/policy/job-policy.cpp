// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdio.h>
#include <threads.h>

#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/exception.h>
#include <lib/zx/handle.h>
#include <lib/zx/job.h>
#include <lib/zx/port.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <lib/zx/vmar.h>

#include <fbl/algorithm.h>

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
    if (zx::job::create(*zx::job::default_job(), 0u, &job) != ZX_OK)
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

static bool AbsThenRel() {
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

static bool InvalidCallsAbs() {
    BEGIN_TEST;

    invalid_calls(ZX_JOB_POL_ABSOLUTE);

    END_TEST;
}

static bool InvalidCallsRel() {
    BEGIN_TEST;

    invalid_calls(ZX_JOB_POL_RELATIVE);

    END_TEST;
}

// Test that executing the given mini-process.h command (|minip_cmd|)
// produces the given result (|expect|) when the given policy is in force.
static bool TestInvokingPolicy(
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

static bool EnforceDenyEvent() {
    BEGIN_TEST;

    zx_policy_basic_t policy[] = { { ZX_POL_NEW_EVENT, ZX_POL_ACTION_DENY } };
    TestInvokingPolicy(policy, static_cast<uint32_t>(fbl::count_of(policy)), MINIP_CMD_CREATE_EVENT,
                         ZX_ERR_ACCESS_DENIED);

    END_TEST;
}

static bool EnforceDenyProfile() {
    BEGIN_TEST;

    zx_policy_basic_t policy[] = {{ZX_POL_NEW_PROFILE, ZX_POL_ACTION_DENY}};
    TestInvokingPolicy(policy, static_cast<uint32_t>(fbl::count_of(policy)),
                       MINIP_CMD_CREATE_PROFILE, ZX_ERR_ACCESS_DENIED);

    END_TEST;
}

static bool EnforceDenyChannel() {
    BEGIN_TEST;

    zx_policy_basic_t policy[] = { { ZX_POL_NEW_CHANNEL, ZX_POL_ACTION_DENY } };
    TestInvokingPolicy(policy, static_cast<uint32_t>(fbl::count_of(policy)), MINIP_CMD_CREATE_CHANNEL,
                         ZX_ERR_ACCESS_DENIED);

    END_TEST;
}

static bool EnforceDenyPagerVmo() {
    BEGIN_TEST;

    zx_policy_basic_t policy[] = {{ZX_POL_NEW_VMO, ZX_POL_ACTION_DENY}};
    TestInvokingPolicy(policy, static_cast<uint32_t>(fbl::count_of(policy)),
                       MINIP_CMD_CREATE_PAGER_VMO, ZX_ERR_ACCESS_DENIED);

    END_TEST;
}

static bool EnforceDenyVmoContiguous() {
    BEGIN_TEST;

    zx_policy_basic_t policy[] = {{ZX_POL_NEW_VMO, ZX_POL_ACTION_DENY}};
    TestInvokingPolicy(policy, static_cast<uint32_t>(fbl::count_of(policy)),
                       MINIP_CMD_CREATE_VMO_CONTIGUOUS, ZX_ERR_ACCESS_DENIED);

    END_TEST;
}

static bool EnforceDenyVmoPhysical() {
    BEGIN_TEST;

    zx_policy_basic_t policy[] = {{ZX_POL_NEW_VMO, ZX_POL_ACTION_DENY}};
    TestInvokingPolicy(policy, static_cast<uint32_t>(fbl::count_of(policy)),
                       MINIP_CMD_CREATE_VMO_PHYSICAL, ZX_ERR_ACCESS_DENIED);

    END_TEST;
}

static bool EnforceDenyAny() {
    BEGIN_TEST;

    zx_policy_basic_t policy[] = {{ZX_POL_NEW_ANY, ZX_POL_ACTION_DENY}};
    TestInvokingPolicy(policy, static_cast<uint32_t>(fbl::count_of(policy)),
                       MINIP_CMD_CREATE_EVENT, ZX_ERR_ACCESS_DENIED);
    TestInvokingPolicy(policy, static_cast<uint32_t>(fbl::count_of(policy)),
                       MINIP_CMD_CREATE_PROFILE, ZX_ERR_ACCESS_DENIED);
    TestInvokingPolicy(policy, static_cast<uint32_t>(fbl::count_of(policy)),
                       MINIP_CMD_CREATE_CHANNEL, ZX_ERR_ACCESS_DENIED);

    END_TEST;
}

static bool EnforceAllowAny() {
    BEGIN_TEST;

    zx_policy_basic_t policy[] = { { ZX_POL_NEW_ANY, ZX_POL_ACTION_ALLOW } };
    TestInvokingPolicy(policy, static_cast<uint32_t>(fbl::count_of(policy)), MINIP_CMD_CREATE_EVENT,
                         ZX_OK);

    END_TEST;
}

static bool EnforceDenyButEvent() {
    BEGIN_TEST;

    zx_policy_basic_t policy[] = {
        { ZX_POL_NEW_ANY, ZX_POL_ACTION_DENY },
        { ZX_POL_NEW_EVENT, ZX_POL_ACTION_ALLOW }
    };
    TestInvokingPolicy(policy, static_cast<uint32_t>(fbl::count_of(policy)), MINIP_CMD_CREATE_EVENT,
                         ZX_OK);
    TestInvokingPolicy(policy, static_cast<uint32_t>(fbl::count_of(policy)), MINIP_CMD_CREATE_CHANNEL,
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

namespace {

enum class ExceptionTestType {
    kPorts,
    kChannels
};

} // namespace

// Like TestInvokingPolicy(), this tests that executing the given
// mini-process.h command produces the given result when the given policy
// is in force.  In addition, it tests that a debug port exception gets
// generated.
static bool TestInvokingPolicyWithException(
    ExceptionTestType test_type, const zx_policy_basic_t* policy,
    uint32_t policy_count, uint32_t minip_cmd,
    zx_status_t expected_syscall_result) {
    auto job = make_job();
    ASSERT_EQ(job.set_policy(ZX_JOB_POL_ABSOLUTE, ZX_JOB_POL_BASIC, policy,
                             policy_count), ZX_OK);

    zx_handle_t ctrl;
    zx::thread thread;
    auto proc = make_test_process(job, &thread, &ctrl);
    ASSERT_TRUE(proc.is_valid());
    ASSERT_NE(ctrl, ZX_HANDLE_INVALID);

    zx_handle_t exc_port = ZX_HANDLE_INVALID;
    zx::channel exc_channel;
    if (test_type == ExceptionTestType::kPorts) {
        ASSERT_EQ(zx_port_create(0, &exc_port), ZX_OK);
        ASSERT_EQ(zx_task_bind_exception_port(
                      proc.get(), exc_port, kExceptionPortKey,
                      ZX_EXCEPTION_PORT_DEBUGGER),
                  ZX_OK);
    } else {
        ASSERT_EQ(proc.create_exception_channel(ZX_EXCEPTION_PORT_DEBUGGER, &exc_channel), ZX_OK);
    }

    EXPECT_EQ(mini_process_cmd_send(ctrl, minip_cmd), ZX_OK);

    // Check that the subprocess did not return a reply yet (indicating
    // that it was suspended).
    EXPECT_EQ(zx_object_wait_one(ctrl, ZX_CHANNEL_READABLE,
                                 zx_deadline_after(ZX_MSEC(1)), nullptr),
              ZX_ERR_TIMED_OUT);

    zx_koid_t pid;
    zx_koid_t tid;
    ASSERT_TRUE(get_koid(proc.get(), &pid));
    ASSERT_TRUE(get_koid(thread.get(), &tid));

    // Check that we receive an exception message.
    zx::exception exception;
    if (test_type == ExceptionTestType::kPorts) {
        zx_port_packet_t packet;
        ASSERT_EQ(zx_port_wait(exc_port, ZX_TIME_INFINITE, &packet), ZX_OK);

        // Check the exception message contents.
        ASSERT_EQ(packet.key, kExceptionPortKey);
        ASSERT_EQ(packet.type, (uint32_t)ZX_EXCP_POLICY_ERROR);
        ASSERT_EQ(packet.exception.pid, pid);
        ASSERT_EQ(packet.exception.tid, tid);
    } else {
        zx_exception_info_t info;
        ASSERT_EQ(exc_channel.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), nullptr), ZX_OK);
        ASSERT_EQ(exc_channel.read(0, &info, sizeof(info), nullptr,
                                   exception.reset_and_get_address(), 1, nullptr),
                  ZX_OK);

        ASSERT_EQ(info.type, ZX_EXCP_POLICY_ERROR);
        ASSERT_EQ(info.tid, tid);
        ASSERT_EQ(info.pid, pid);

        // Make sure the exception has the correct task handles.
        zx::thread exception_thread;
        zx::process exception_process;
        ASSERT_EQ(exception.get_thread(&exception_thread), ZX_OK);
        ASSERT_EQ(exception.get_process(&exception_process), ZX_OK);

        zx_koid_t handle_tid = ZX_KOID_INVALID;
        EXPECT_TRUE(get_koid(exception_thread.get(), &handle_tid));
        EXPECT_EQ(handle_tid, tid);

        zx_koid_t handle_pid = ZX_KOID_INVALID;
        EXPECT_TRUE(get_koid(exception_process.get(), &handle_pid));
        EXPECT_EQ(handle_pid, pid);
    }

    // Check that we can read the thread's register state.
    zx_thread_state_general_regs_t regs;
    ASSERT_EQ(zx_thread_read_state(thread.get(),
                                   ZX_THREAD_STATE_GENERAL_REGS, &regs,
                                   sizeof(regs)),
              ZX_OK);
    ASSERT_EQ(get_syscall_result(&regs), (uint64_t)expected_syscall_result);
    // TODO(mseaborn): Check the values of other registers.  We could check
    // that rip/pc is within the VDSO, which will require figuring out
    // where the VDSO is mapped.  We could check that unwinding the stack
    // using crashlogger gives a correct backtrace.

    // Resume the thread.
    if (test_type == ExceptionTestType::kPorts) {
        ASSERT_EQ(zx_task_resume_from_exception(thread.get(), exc_port, 0), ZX_OK);
    } else {
        uint32_t state = ZX_EXCEPTION_STATE_HANDLED;
        ASSERT_EQ(exception.set_property(ZX_PROP_EXCEPTION_STATE, &state, sizeof(state)), ZX_OK);
        exception.reset();
    }

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

// Invokes a policy exception test using both port and channel exceptions.
static void TestInvokingPolicyWithException(
    const zx_policy_basic_t* policy, uint32_t policy_count, uint32_t minip_cmd,
    zx_status_t expected_syscall_result) {
    EXPECT_TRUE(TestInvokingPolicyWithException(
        ExceptionTestType::kPorts, policy, policy_count, minip_cmd,
        expected_syscall_result));

    EXPECT_TRUE(TestInvokingPolicyWithException(
        ExceptionTestType::kChannels, policy, policy_count, minip_cmd,
        expected_syscall_result));
}

static bool TestExceptionOnNewEventAndDeny() {
    BEGIN_TEST;

    zx_policy_basic_t policy[] = {
        { ZX_POL_NEW_EVENT, ZX_POL_ACTION_DENY | ZX_POL_ACTION_EXCEPTION },
    };
    TestInvokingPolicyWithException(
        policy, static_cast<uint32_t>(fbl::count_of(policy)), MINIP_CMD_CREATE_EVENT, ZX_ERR_ACCESS_DENIED);

    END_TEST;
}

static bool TestExceptionOnNewEventButAllow() {
    BEGIN_TEST;

    zx_policy_basic_t policy[] = {
        { ZX_POL_NEW_EVENT, ZX_POL_ACTION_ALLOW | ZX_POL_ACTION_EXCEPTION },
    };
    TestInvokingPolicyWithException(
        policy, static_cast<uint32_t>(fbl::count_of(policy)), MINIP_CMD_CREATE_EVENT, ZX_OK);

    END_TEST;
}

static bool TestExceptionOnNewProfileAndDeny() {
    BEGIN_TEST;

    zx_policy_basic_t policy[] = {
        {ZX_POL_NEW_PROFILE, ZX_POL_ACTION_DENY | ZX_POL_ACTION_EXCEPTION},
    };
    TestInvokingPolicyWithException(policy, static_cast<uint32_t>(fbl::count_of(policy)),
                                    MINIP_CMD_CREATE_PROFILE, ZX_ERR_ACCESS_DENIED);

    END_TEST;
}

// Test ZX_POL_BAD_HANDLE when syscalls are allowed to continue.
static bool TestErrorOnBadHandle() {
    BEGIN_TEST;

    // The ALLOW and DENY actions should be equivalent for ZX_POL_BAD_HANDLE.
    uint32_t actions[] = { ZX_POL_ACTION_ALLOW, ZX_POL_ACTION_DENY };
    for (uint32_t action : actions) {
        unittest_printf_critical("Testing action=%d\n", action);
        zx_policy_basic_t policy[] = {
            { ZX_POL_BAD_HANDLE, action },
        };
        TestInvokingPolicy(
            policy, static_cast<uint32_t>(fbl::count_of(policy)), MINIP_CMD_USE_BAD_HANDLE_CLOSED,
            ZX_ERR_BAD_HANDLE);
        TestInvokingPolicy(
            policy, static_cast<uint32_t>(fbl::count_of(policy)), MINIP_CMD_USE_BAD_HANDLE_TRANSFERRED,
            ZX_ERR_BAD_HANDLE);
    }

    END_TEST;
}

// Test ZX_POL_BAD_HANDLE with ZX_POL_ACTION_EXCEPTION.
static bool TestExceptionOnBadHandle() {
    BEGIN_TEST;

    // The ALLOW and DENY actions should be equivalent for ZX_POL_BAD_HANDLE.
    uint32_t actions[] = { ZX_POL_ACTION_ALLOW, ZX_POL_ACTION_DENY };
    for (uint32_t action : actions) {
        unittest_printf_critical("Testing action=%d\n", action);
        zx_policy_basic_t policy[] = {
            { ZX_POL_BAD_HANDLE, action | ZX_POL_ACTION_EXCEPTION },
        };
        TestInvokingPolicyWithException(
            policy, static_cast<uint32_t>(fbl::count_of(policy)), MINIP_CMD_USE_BAD_HANDLE_CLOSED,
            ZX_ERR_BAD_HANDLE);
        TestInvokingPolicyWithException(
            policy, static_cast<uint32_t>(fbl::count_of(policy)), MINIP_CMD_USE_BAD_HANDLE_TRANSFERRED,
            ZX_ERR_BAD_HANDLE);
    }

    END_TEST;
}

// The one exception for ZX_POL_BAD_HANDLE is zx_object_info( ZX_INFO_HANDLE_VALID).
static bool TestGetInfoOnBadHandle() {
    BEGIN_TEST;

    zx_policy_basic_t policy[] = {{
        ZX_POL_BAD_HANDLE, ZX_POL_ACTION_DENY | ZX_POL_ACTION_EXCEPTION }};
    TestInvokingPolicy(
        policy, static_cast<uint32_t>(fbl::count_of(policy)), MINIP_CMD_VALIDATE_CLOSED_HANDLE,
        ZX_ERR_BAD_HANDLE);

    END_TEST;
}

BEGIN_TEST_CASE(job_policy)
RUN_TEST(InvalidCallsAbs)
RUN_TEST(InvalidCallsRel)
RUN_TEST(AbsThenRel)
RUN_TEST(EnforceDenyEvent)
RUN_TEST(EnforceDenyProfile)
RUN_TEST(EnforceDenyChannel)
RUN_TEST(EnforceDenyPagerVmo)
RUN_TEST(EnforceDenyVmoContiguous)
RUN_TEST(EnforceDenyVmoPhysical)
RUN_TEST(EnforceDenyAny)
RUN_TEST(EnforceAllowAny)
RUN_TEST(EnforceDenyButEvent)
RUN_TEST(TestExceptionOnNewEventAndDeny)
RUN_TEST(TestExceptionOnNewEventButAllow)
RUN_TEST(TestExceptionOnNewProfileAndDeny)
RUN_TEST(TestErrorOnBadHandle)
RUN_TEST(TestExceptionOnBadHandle)
RUN_TEST(TestGetInfoOnBadHandle)
END_TEST_CASE(job_policy)
