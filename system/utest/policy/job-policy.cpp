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

#include <mxtl/type_support.h>

#include <magenta/process.h>
#include <magenta/syscalls/debug.h>
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
        MX_JOB_POL_ABSOLUTE, MX_JOB_POL_BASIC, policy, countof(policy)), MX_OK, "");

    // A contradictory policy should fail.
    policy[0].policy = MX_POL_ACTION_EXCEPTION | MX_POL_ACTION_DENY;
    EXPECT_EQ(job.set_policy(
        MX_JOB_POL_ABSOLUTE, MX_JOB_POL_BASIC, policy, countof(policy)), MX_ERR_ALREADY_EXISTS, "");

    // The same again will succeed.
    policy[0].policy = MX_POL_ACTION_KILL;
    EXPECT_EQ(job.set_policy(
        MX_JOB_POL_ABSOLUTE, MX_JOB_POL_BASIC, policy, countof(policy)), MX_OK, "");

    // A contradictory relative policy will succeed, but is a no-op
    policy[0].policy = MX_POL_ACTION_ALLOW;
    EXPECT_EQ(job.set_policy(
        MX_JOB_POL_RELATIVE, MX_JOB_POL_BASIC, policy, countof(policy)), MX_OK, "");

    mx_policy_basic_t more[] = {
        { MX_POL_NEW_CHANNEL, MX_POL_ACTION_ALLOW | MX_POL_ACTION_EXCEPTION },
        { MX_POL_NEW_FIFO, MX_POL_ACTION_DENY } };

    // An additional absolute policy that doesn't contradict existing
    // policy can be added.
    EXPECT_EQ(job.set_policy(
        MX_JOB_POL_ABSOLUTE, MX_JOB_POL_BASIC, more, countof(more)), MX_OK, "");

    END_TEST;
}

static bool invalid_calls(uint32_t options) {
    BEGIN_TEST;

    auto job = make_job();

    EXPECT_EQ(job.set_policy(options, MX_JOB_POL_BASIC, nullptr, 0u), MX_ERR_INVALID_ARGS, "");

    EXPECT_EQ(job.set_policy(options, MX_JOB_POL_BASIC, nullptr, 5u), MX_ERR_INVALID_ARGS, "");

    mx_policy_basic_t policy1[] = {
        { MX_POL_BAD_HANDLE, MX_POL_ACTION_KILL },
    };

    EXPECT_EQ(job.set_policy(options, MX_JOB_POL_BASIC, policy1, 0u), MX_ERR_INVALID_ARGS, "");

    mx_policy_basic_t policy2[] = {
        { 100001u, MX_POL_ACTION_KILL },
    };

    EXPECT_EQ(job.set_policy(
        options, MX_JOB_POL_BASIC, policy2, countof(policy2)), MX_ERR_INVALID_ARGS, "");

    mx_policy_basic_t policy3[] = {
        { MX_POL_BAD_HANDLE, 100001u },
    };

    EXPECT_EQ(job.set_policy(
        options, MX_JOB_POL_BASIC, policy3, countof(policy2)), MX_ERR_NOT_SUPPORTED, "");

    // The job will still accept a valid combination:
    mx_policy_basic_t policy4[] = {
        { MX_POL_BAD_HANDLE, MX_POL_ACTION_KILL } };

    EXPECT_EQ(job.set_policy(
        options, MX_JOB_POL_BASIC, policy4, countof(policy4)), MX_OK, "");

    END_TEST;
}

static bool invalid_calls_abs() {
    return invalid_calls(MX_JOB_POL_ABSOLUTE);
}

static bool invalid_calls_rel() {
    return invalid_calls(MX_JOB_POL_RELATIVE);
}

static bool enforce_creation_pol(
    mx_policy_basic_t* pol, uint32_t pol_count, uint32_t minip_cmd, mx_status_t expect) {
    BEGIN_TEST;
    auto job = make_job();

    ASSERT_EQ(job.set_policy(
        MX_JOB_POL_ABSOLUTE, MX_JOB_POL_BASIC, pol, pol_count), MX_OK, "");

    mx_handle_t ctrl;
    auto proc = make_test_process(job, nullptr, &ctrl);
    ASSERT_TRUE(proc.is_valid(), "");
    ASSERT_NEQ(ctrl, MX_HANDLE_INVALID, "");

    mx_handle_t obj;
    EXPECT_EQ(mini_process_cmd(ctrl, minip_cmd, &obj), expect, "");
    EXPECT_EQ(mini_process_cmd(ctrl, MINIP_CMD_EXIT_NORMAL, nullptr), MX_ERR_PEER_CLOSED, "");

    mx_handle_close(ctrl);
    END_TEST;
}

static bool enforce_deny_event() {
    mx_policy_basic_t policy[] = { { MX_POL_NEW_EVENT, MX_POL_ACTION_DENY } };
    return enforce_creation_pol(
        policy, countof(policy), MINIP_CMD_CREATE_EVENT, MX_ERR_ACCESS_DENIED);
}

static bool enforce_deny_channel() {
    mx_policy_basic_t policy[] = { { MX_POL_NEW_CHANNEL, MX_POL_ACTION_DENY } };
    return enforce_creation_pol(
        policy, countof(policy), MINIP_CMD_CREATE_CHANNEL, MX_ERR_ACCESS_DENIED);
}

static bool enforce_deny_any() {
    mx_policy_basic_t policy[] = { { MX_POL_NEW_ANY, MX_POL_ACTION_DENY } };
    return enforce_creation_pol(
        policy, countof(policy), MINIP_CMD_CREATE_EVENT, MX_ERR_ACCESS_DENIED);
}

static bool enforce_allow_any() {
    mx_policy_basic_t policy[] = { { MX_POL_NEW_ANY, MX_POL_ACTION_ALLOW } };
    return enforce_creation_pol(
        policy, countof(policy), MINIP_CMD_CREATE_EVENT, MX_OK);
}

static bool enforce_deny_but_event() {
    mx_policy_basic_t policy[] = {
        { MX_POL_NEW_ANY, MX_POL_ACTION_DENY },
        { MX_POL_NEW_EVENT, MX_POL_ACTION_ALLOW }
    };
    auto res = enforce_creation_pol(
        policy, countof(policy), MINIP_CMD_CREATE_EVENT, MX_OK);
    return res && enforce_creation_pol(
        policy, countof(policy), MINIP_CMD_CREATE_CHANNEL, MX_ERR_ACCESS_DENIED);
}

static bool get_koid(mx_handle_t handle, mx_koid_t* koid) {
    mx_info_handle_basic_t info;
    ASSERT_EQ(mx_object_get_info(
                  handle, MX_INFO_HANDLE_BASIC, &info, sizeof(info),
                  nullptr, nullptr), MX_OK, "");
    *koid = info.koid;
    return true;
}

#if defined(__x86_64__)

#define ARCH_ID ARCH_ID_X86_64

typedef struct mx_x86_64_general_regs mx_general_regs_t;

static uint64_t get_syscall_result(mx_general_regs_t* regs) {
    return regs->rax;
}

#elif defined(__aarch64__)

#define ARCH_ID ARCH_ID_ARM_64

typedef struct mx_arm64_general_regs mx_general_regs_t;

static uint64_t get_syscall_result(mx_general_regs_t* regs) {
    return regs->r[0];
}

#else
# error Unsupported architecture
#endif

static bool test_exception_on_new_event(uint32_t base_policy,
                                        mx_status_t expected_syscall_result) {
    BEGIN_TEST;

    mx_policy_basic_t policy[] = {
        { MX_POL_NEW_EVENT, base_policy | MX_POL_ACTION_EXCEPTION },
    };
    auto job = make_job();
    ASSERT_EQ(job.set_policy(MX_JOB_POL_ABSOLUTE, MX_JOB_POL_BASIC, policy,
                             countof(policy)), MX_OK, "");

    mx_handle_t ctrl;
    mx::thread thread;
    auto proc = make_test_process(job, &thread, &ctrl);
    ASSERT_TRUE(proc.is_valid(), "");
    ASSERT_NEQ(ctrl, MX_HANDLE_INVALID, "");

    mx_handle_t exc_port;
    ASSERT_EQ(mx_port_create(0, &exc_port), MX_OK, "");
    ASSERT_EQ(mx_task_bind_exception_port(
                  proc.get(), exc_port, kExceptionPortKey,
                  MX_EXCEPTION_PORT_DEBUGGER),
              MX_OK, "");

    EXPECT_EQ(mini_process_cmd_send(ctrl, MINIP_CMD_CREATE_EVENT), MX_OK, "");

    // Check that the subprocess did not return a reply yet (indicating
    // that it was suspended).
    EXPECT_EQ(mx_object_wait_one(ctrl, MX_CHANNEL_READABLE,
                                 mx_deadline_after(MX_MSEC(1)), nullptr),
              MX_ERR_TIMED_OUT, "");

    // Check that we receive an exception message.
    mx_exception_packet_t packet;
    ASSERT_EQ(mx_port_wait(exc_port, MX_TIME_INFINITE, &packet, sizeof(packet)),
              MX_OK, "");

    // Check the exception message contents.
    ASSERT_EQ(packet.hdr.key, kExceptionPortKey, "");
    ASSERT_EQ(packet.report.header.type, (uint32_t)MX_EXCP_GENERAL, "");
    ASSERT_EQ(packet.report.context.arch_id, ARCH_ID, "");
    mx_koid_t pid;
    mx_koid_t tid;
    ASSERT_TRUE(get_koid(proc.get(), &pid), "");
    ASSERT_TRUE(get_koid(thread.get(), &tid), "");
    ASSERT_EQ(packet.report.context.pid, pid, "");
    ASSERT_EQ(packet.report.context.tid, tid, "");
    // TODO(mseaborn): Implement reporting the pc register via this message.
    ASSERT_EQ(packet.report.context.arch.pc, 0, "");
    // Check that all of these other fields are zero.
    for (uint32_t idx = 0; idx < sizeof(packet.report.context.arch.u); ++idx) {
        EXPECT_EQ(
            reinterpret_cast<uint8_t*>(&packet.report.context.arch.u)[idx],
            0, "");
    }

    // Check that we can read the thread's register state.
    mx_general_regs_t regs;
    uint32_t size_read;
    ASSERT_EQ(mx_thread_read_state(thread.get(), MX_THREAD_STATE_REGSET0,
                                   &regs, sizeof(regs), &size_read),
              MX_OK, "");
    ASSERT_EQ(size_read, sizeof(regs), "");
    ASSERT_EQ(get_syscall_result(&regs), (uint64_t)expected_syscall_result, "");
    // TODO(mseaborn): Check the values of other registers.  We could check
    // that rip/pc is within the VDSO, which will require figuring out
    // where the VDSO is mapped.  We could check that unwinding the stack
    // using crashlogger gives a correct backtrace.

    // Resume the thread.
    ASSERT_EQ(mx_task_resume(thread.get(), MX_RESUME_EXCEPTION), MX_OK, "");
    // Check that the read-ready state of the channel changed compared with
    // the earlier check.
    EXPECT_EQ(mx_object_wait_one(ctrl, MX_CHANNEL_READABLE, MX_TIME_INFINITE,
                                 nullptr),
              MX_OK, "");

    // Check that we receive a reply message from the resumed thread.
    mx_handle_t obj;
    EXPECT_EQ(mini_process_cmd_read_reply(ctrl, &obj),
              expected_syscall_result, "");
    if (expected_syscall_result == MX_OK)
        EXPECT_EQ(mx_handle_close(obj), MX_OK, "");

    // Clean up: Tell the subprocess to exit.
    EXPECT_EQ(mini_process_cmd(ctrl, MINIP_CMD_EXIT_NORMAL, nullptr),
              MX_ERR_PEER_CLOSED, "");

    mx_handle_close(ctrl);

    END_TEST;
}

static bool test_exception_on_new_event_and_deny() {
    return test_exception_on_new_event(MX_POL_ACTION_DENY,
                                       MX_ERR_ACCESS_DENIED);
}

static bool test_exception_on_new_event_but_allow() {
    return test_exception_on_new_event(MX_POL_ACTION_ALLOW, MX_OK);
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
END_TEST_CASE(job_policy)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
