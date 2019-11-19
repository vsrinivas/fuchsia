// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <threads.h>
#include <unistd.h>

#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/exception.h>
#include <lib/zx/handle.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <lib/zx/vmar.h>

#include <zircon/process.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/policy.h>
#include <zircon/syscalls/port.h>

#include <fbl/algorithm.h>
#include <mini-process/mini-process.h>
#include <zxtest/zxtest.h>

namespace {

// Basic job operation is tested by core-tests.
static zx::job MakeJob() {
  zx::job job;
  if (zx::job::create(*zx::job::default_job(), 0u, &job) != ZX_OK)
    return zx::job();
  return job;
}

zx::process MakeTestProcess(const zx::job& job, zx::thread* out_thread, zx_handle_t* ctrl) {
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
  status = start_mini_process_etc(proc.get(), thr, vmar.get(), event.release(), true, ctrl);
  if (status != ZX_OK)
    return zx::process();

  return proc;
}

TEST(JobPolicyTest, AbsThenRel) {
  zx_policy_basic_v1_t policy[] = {{ZX_POL_BAD_HANDLE, ZX_POL_ACTION_KILL}};

  auto job = MakeJob();
  ASSERT_OK(job.set_policy(ZX_JOB_POL_ABSOLUTE, ZX_JOB_POL_BASIC, policy,
                           static_cast<uint32_t>(fbl::count_of(policy))));

  // A contradictory policy should fail.
  policy[0].policy = ZX_POL_ACTION_DENY_EXCEPTION;
  ASSERT_EQ(job.set_policy(ZX_JOB_POL_ABSOLUTE, ZX_JOB_POL_BASIC, policy,
                           static_cast<uint32_t>(fbl::count_of(policy))),
            ZX_ERR_ALREADY_EXISTS);

  // The same again will succeed.
  policy[0].policy = ZX_POL_ACTION_KILL;
  ASSERT_OK(job.set_policy(ZX_JOB_POL_ABSOLUTE, ZX_JOB_POL_BASIC, policy,
                           static_cast<uint32_t>(fbl::count_of(policy))));

  // A contradictory relative policy will succeed, but is a no-op
  policy[0].policy = ZX_POL_ACTION_ALLOW;
  ASSERT_OK(job.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_BASIC, policy,
                           static_cast<uint32_t>(fbl::count_of(policy))));

  zx_policy_basic_v1_t more[] = {{ZX_POL_NEW_CHANNEL, ZX_POL_ACTION_ALLOW_EXCEPTION},
                                 {ZX_POL_NEW_FIFO, ZX_POL_ACTION_DENY}};

  // An additional absolute policy that doesn't contradict existing
  // policy can be added.
  ASSERT_OK(job.set_policy(ZX_JOB_POL_ABSOLUTE, ZX_JOB_POL_BASIC, more,
                           static_cast<uint32_t>(fbl::count_of(more))));
}

void InvalidCalls(uint32_t options) {
  {
    // Null policy pointer.
    auto job = MakeJob();
    EXPECT_EQ(job.set_policy(options, ZX_JOB_POL_BASIC, nullptr, 0u), ZX_ERR_INVALID_ARGS);
    EXPECT_EQ(job.set_policy(options, ZX_JOB_POL_BASIC, nullptr, 1u), ZX_ERR_INVALID_ARGS);
    EXPECT_EQ(job.set_policy(options, ZX_JOB_POL_BASIC, nullptr, 5u), ZX_ERR_INVALID_ARGS);

    zx_policy_basic_v1_t policy[] = {{ZX_POL_BAD_HANDLE, ZX_POL_ACTION_KILL}};
    EXPECT_OK(job.set_policy(options, ZX_JOB_POL_BASIC, policy, 1u));
    EXPECT_EQ(job.set_policy(options, ZX_JOB_POL_BASIC, policy, 33u), ZX_ERR_INVALID_ARGS);
  }

  {
    // Count is 0.
    auto job = MakeJob();
    zx_policy_basic_v1_t policy[] = {{ZX_POL_BAD_HANDLE, ZX_POL_ACTION_KILL}};
    EXPECT_EQ(job.set_policy(options, ZX_JOB_POL_BASIC, policy, 0u), ZX_ERR_INVALID_ARGS);

    EXPECT_OK(job.set_policy(options, ZX_JOB_POL_BASIC, policy, 1u));
  }

  {
    // Too many.
    auto job = MakeJob();
    zx_policy_basic_v1_t policy[16]{};
    for (unsigned i = 0; i < fbl::count_of(policy); ++i) {
      policy[i] = {ZX_POL_BAD_HANDLE, ZX_POL_ACTION_KILL};
    }
    EXPECT_EQ(job.set_policy(options, ZX_JOB_POL_BASIC, policy, fbl::count_of(policy)),
              ZX_ERR_OUT_OF_RANGE);

    EXPECT_OK(job.set_policy(options, ZX_JOB_POL_BASIC, policy, 1u));
  }

  {
    // Invalid condition value.
    auto job = MakeJob();
    zx_policy_basic_v1_t policy[] = {{100001u, ZX_POL_ACTION_KILL}};
    EXPECT_EQ(job.set_policy(options, ZX_JOB_POL_BASIC, policy,
                             static_cast<uint32_t>(fbl::count_of(policy))),
              ZX_ERR_INVALID_ARGS);

    zx_policy_basic_v1_t good_policy[] = {{ZX_POL_BAD_HANDLE, ZX_POL_ACTION_KILL}};
    EXPECT_OK(job.set_policy(options, ZX_JOB_POL_BASIC, good_policy, 1u));
  }

  {
    // Invalid action value.
    auto job = MakeJob();
    zx_policy_basic_v1_t policy[] = {{ZX_POL_BAD_HANDLE, ZX_POL_ACTION_KILL + 1}};
    EXPECT_EQ(job.set_policy(options, ZX_JOB_POL_BASIC, policy,
                             static_cast<uint32_t>(fbl::count_of(policy))),
              ZX_ERR_NOT_SUPPORTED);

    zx_policy_basic_v1_t good_policy[] = {{ZX_POL_BAD_HANDLE, ZX_POL_ACTION_KILL}};
    EXPECT_OK(job.set_policy(options, ZX_JOB_POL_BASIC, good_policy, 1u));
  }
}

TEST(JobPolicyTest, InvalidCallsAbs) { InvalidCalls(ZX_JOB_POL_ABSOLUTE); }

TEST(JobPolicyTest, InvalidCallsRel) { InvalidCalls(ZX_JOB_POL_RELATIVE); }

// Checks that executing the given mini-process.h command (|minip_cmd|) produces the given result
// (|expect|) when the given policy is in force.
void CheckInvokingPolicy(zx_policy_basic_v1_t* pol, uint32_t pol_count, uint32_t minip_cmd,
                         zx_status_t expect) {
  auto job = MakeJob();
  ASSERT_OK(job.set_policy(ZX_JOB_POL_ABSOLUTE, ZX_JOB_POL_BASIC, pol, pol_count));

  zx_handle_t ctrl;
  auto proc = MakeTestProcess(job, nullptr, &ctrl);
  ASSERT_TRUE(proc.is_valid());
  ASSERT_NE(ctrl, ZX_HANDLE_INVALID);
  zx_handle_t obj;
  EXPECT_EQ(mini_process_cmd(ctrl, minip_cmd, &obj), expect);
  EXPECT_EQ(mini_process_cmd(ctrl, MINIP_CMD_EXIT_NORMAL, nullptr), ZX_ERR_PEER_CLOSED);

  zx_handle_close(ctrl);
}

TEST(JobPolicyTest, EnforceDenyEvent) {
  zx_policy_basic_v1_t policy[] = {{ZX_POL_NEW_EVENT, ZX_POL_ACTION_DENY}};
  CheckInvokingPolicy(policy, static_cast<uint32_t>(fbl::count_of(policy)), MINIP_CMD_CREATE_EVENT,
                      ZX_ERR_ACCESS_DENIED);
}

TEST(JobPolicyTest, EnforceDenyProfile) {
  zx_policy_basic_v1_t policy[] = {{ZX_POL_NEW_PROFILE, ZX_POL_ACTION_DENY}};
  CheckInvokingPolicy(policy, static_cast<uint32_t>(fbl::count_of(policy)),
                      MINIP_CMD_CREATE_PROFILE, ZX_ERR_ACCESS_DENIED);
}

TEST(JobPolicyTest, EnforceDenyChannel) {
  zx_policy_basic_v1_t policy[] = {{ZX_POL_NEW_CHANNEL, ZX_POL_ACTION_DENY}};
  CheckInvokingPolicy(policy, static_cast<uint32_t>(fbl::count_of(policy)),
                      MINIP_CMD_CREATE_CHANNEL, ZX_ERR_ACCESS_DENIED);
}

TEST(JobPolicyTest, EnforceDenyPagerVmo) {
  zx_policy_basic_v1_t policy[] = {{ZX_POL_NEW_VMO, ZX_POL_ACTION_DENY}};
  CheckInvokingPolicy(policy, static_cast<uint32_t>(fbl::count_of(policy)),
                      MINIP_CMD_CREATE_PAGER_VMO, ZX_ERR_ACCESS_DENIED);
}

TEST(JobPolicyTest, EnforceDenyVmoContiguous) {
  zx_policy_basic_v1_t policy[] = {{ZX_POL_NEW_VMO, ZX_POL_ACTION_DENY}};
  CheckInvokingPolicy(policy, static_cast<uint32_t>(fbl::count_of(policy)),
                      MINIP_CMD_CREATE_VMO_CONTIGUOUS, ZX_ERR_ACCESS_DENIED);
}

TEST(JobPolicyTest, EnforceDenyVmoPhysical) {
  zx_policy_basic_v1_t policy[] = {{ZX_POL_NEW_VMO, ZX_POL_ACTION_DENY}};
  CheckInvokingPolicy(policy, static_cast<uint32_t>(fbl::count_of(policy)),
                      MINIP_CMD_CREATE_VMO_PHYSICAL, ZX_ERR_ACCESS_DENIED);
}

TEST(JobPolicyTest, EnforceDenyAmbientExecutable) {
  zx_policy_basic_v1_t policy[] = {{ZX_POL_AMBIENT_MARK_VMO_EXEC, ZX_POL_ACTION_DENY}};
  CheckInvokingPolicy(policy, static_cast<uint32_t>(fbl::count_of(policy)),
                      MINIP_CMD_ATTEMPT_AMBIENT_EXECUTABLE, ZX_ERR_ACCESS_DENIED);
}

TEST(JobPolicyTest, TestAllowAmbientExecutable) {
  zx_policy_basic_v1_t policy[] = {{ZX_POL_AMBIENT_MARK_VMO_EXEC, ZX_POL_ACTION_ALLOW}};
  CheckInvokingPolicy(policy, static_cast<uint32_t>(fbl::count_of(policy)),
                      MINIP_CMD_ATTEMPT_AMBIENT_EXECUTABLE, ZX_OK);
}

TEST(JobPolicyTest, EnforceDenyAny) {
  zx_policy_basic_v1_t policy[] = {{ZX_POL_NEW_ANY, ZX_POL_ACTION_DENY}};
  CheckInvokingPolicy(policy, static_cast<uint32_t>(fbl::count_of(policy)), MINIP_CMD_CREATE_EVENT,
                      ZX_ERR_ACCESS_DENIED);
  CheckInvokingPolicy(policy, static_cast<uint32_t>(fbl::count_of(policy)),
                      MINIP_CMD_CREATE_PROFILE, ZX_ERR_ACCESS_DENIED);
  CheckInvokingPolicy(policy, static_cast<uint32_t>(fbl::count_of(policy)),
                      MINIP_CMD_CREATE_CHANNEL, ZX_ERR_ACCESS_DENIED);
}

// Check that executing the given mini-process.h command (|minip_cmd|) kills the process when the
// given policy |pol| is in effect.
void CheckInvokingPolicyKill(zx_policy_basic_v1_t* pol, uint32_t count, uint32_t minip_cmd) {
  auto job = MakeJob();
  ASSERT_OK(job.set_policy(ZX_JOB_POL_ABSOLUTE, ZX_JOB_POL_BASIC, pol, count));

  zx_handle_t ctrl;
  auto proc = MakeTestProcess(job, nullptr, &ctrl);
  ASSERT_TRUE(proc.is_valid());
  ASSERT_NE(ctrl, ZX_HANDLE_INVALID);

  zx_handle_t obj;
  ASSERT_EQ(mini_process_cmd(ctrl, minip_cmd, &obj), ZX_ERR_PEER_CLOSED);
  ASSERT_OK(proc.wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), nullptr));

  zx_info_process_t proc_info;
  ASSERT_OK(proc.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), nullptr, nullptr));
  ASSERT_TRUE(proc_info.exited);
  ASSERT_EQ(proc_info.return_code, ZX_TASK_RETCODE_POLICY_KILL);

  zx_handle_close(ctrl);
}

TEST(JobPolicyTest, EnforceKillEvent) {
  zx_policy_basic_v1_t policy[] = {{ZX_POL_NEW_EVENT, ZX_POL_ACTION_KILL}};
  CheckInvokingPolicyKill(policy, static_cast<uint32_t>(fbl::count_of(policy)),
                          MINIP_CMD_CREATE_EVENT);
}

TEST(JobPolicyTest, EnforceAllowAny) {
  zx_policy_basic_v1_t policy[] = {{ZX_POL_NEW_ANY, ZX_POL_ACTION_ALLOW}};
  CheckInvokingPolicy(policy, static_cast<uint32_t>(fbl::count_of(policy)), MINIP_CMD_CREATE_EVENT,
                      ZX_OK);
}

TEST(JobPolicyTest, EnforceDenyButEvent) {
  zx_policy_basic_v1_t policy[] = {{ZX_POL_NEW_ANY, ZX_POL_ACTION_DENY},
                                   {ZX_POL_NEW_EVENT, ZX_POL_ACTION_ALLOW}};
  CheckInvokingPolicy(policy, static_cast<uint32_t>(fbl::count_of(policy)), MINIP_CMD_CREATE_EVENT,
                      ZX_OK);
  CheckInvokingPolicy(policy, static_cast<uint32_t>(fbl::count_of(policy)),
                      MINIP_CMD_CREATE_CHANNEL, ZX_ERR_ACCESS_DENIED);
}

void get_koid(zx_handle_t handle, zx_koid_t* koid) {
  zx_info_handle_basic_t info;
  ASSERT_OK(
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));
  *koid = info.koid;
}

#if defined(__x86_64__)

uint64_t get_syscall_result(zx_thread_state_general_regs_t* regs) { return regs->rax; }

#elif defined(__aarch64__)

uint64_t get_syscall_result(zx_thread_state_general_regs_t* regs) { return regs->r[0]; }

#else
#error Unsupported architecture
#endif

// Like CheckInvokingPolicy(), this tests that executing the given
// mini-process.h command produces the given result when the given policy
// is in force.  In addition, it tests that a debug channel exception gets
// generated.
void CheckInvokingPolicyWithException(const zx_policy_basic_v1_t* policy, uint32_t policy_count,
                                      uint32_t minip_cmd, zx_status_t expected_syscall_result) {
  auto job = MakeJob();
  ASSERT_OK(job.set_policy(ZX_JOB_POL_ABSOLUTE, ZX_JOB_POL_BASIC, policy, policy_count));

  zx_handle_t ctrl;
  zx::thread thread;
  auto proc = MakeTestProcess(job, &thread, &ctrl);
  ASSERT_TRUE(proc.is_valid());
  ASSERT_NE(ctrl, ZX_HANDLE_INVALID);

  zx::channel exc_channel;
  ASSERT_OK(proc.create_exception_channel(ZX_EXCEPTION_CHANNEL_DEBUGGER, &exc_channel));

  EXPECT_OK(mini_process_cmd_send(ctrl, minip_cmd));

  // Check that the subprocess did not return a reply yet (indicating
  // that it was suspended).
  EXPECT_EQ(zx_object_wait_one(ctrl, ZX_CHANNEL_READABLE, zx_deadline_after(ZX_MSEC(1)), nullptr),
            ZX_ERR_TIMED_OUT);

  zx_koid_t pid;
  zx_koid_t tid;
  get_koid(proc.get(), &pid);
  get_koid(thread.get(), &tid);

  // Check that we receive an exception message.
  zx::exception exception;
  zx_exception_info_t info;
  ASSERT_OK(exc_channel.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), nullptr));
  ASSERT_OK(exc_channel.read(0, &info, exception.reset_and_get_address(), sizeof(info), 1, nullptr,
                             nullptr));

  ASSERT_EQ(info.type, ZX_EXCP_POLICY_ERROR);
  ASSERT_EQ(info.tid, tid);
  ASSERT_EQ(info.pid, pid);

  // Make sure the exception has the correct task handles.
  zx::thread exception_thread;
  zx::process exception_process;
  ASSERT_OK(exception.get_thread(&exception_thread));
  ASSERT_OK(exception.get_process(&exception_process));

  zx_koid_t handle_tid = ZX_KOID_INVALID;
  get_koid(exception_thread.get(), &handle_tid);
  EXPECT_EQ(handle_tid, tid);

  zx_koid_t handle_pid = ZX_KOID_INVALID;
  get_koid(exception_process.get(), &handle_pid);
  EXPECT_EQ(handle_pid, pid);

  // Check that we can read the thread's register state.
  zx_thread_state_general_regs_t regs;
  ASSERT_OK(zx_thread_read_state(thread.get(), ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)));
  ASSERT_EQ(get_syscall_result(&regs), (uint64_t)expected_syscall_result);
  // TODO(mseaborn): Check the values of other registers.  We could check
  // that rip/pc is within the VDSO, which will require figuring out
  // where the VDSO is mapped.  We could check that unwinding the stack
  // using crashlogger gives a correct backtrace.

  // Resume the thread.
  uint32_t state = ZX_EXCEPTION_STATE_HANDLED;
  ASSERT_OK(exception.set_property(ZX_PROP_EXCEPTION_STATE, &state, sizeof(state)));
  exception.reset();

  // Check that the read-ready state of the channel changed compared with
  // the earlier check.
  EXPECT_OK(zx_object_wait_one(ctrl, ZX_CHANNEL_READABLE, ZX_TIME_INFINITE, nullptr));

  // Check that we receive a reply message from the resumed thread.
  zx_handle_t obj;
  EXPECT_EQ(mini_process_cmd_read_reply(ctrl, &obj), expected_syscall_result);
  if (expected_syscall_result == ZX_OK)
    EXPECT_OK(zx_handle_close(obj));

  // Clean up: Tell the subprocess to exit.
  EXPECT_EQ(mini_process_cmd(ctrl, MINIP_CMD_EXIT_NORMAL, nullptr), ZX_ERR_PEER_CLOSED);

  zx_handle_close(ctrl);
}

TEST(JobPolicyTest, TestExceptionOnNewEventAndDeny) {
  zx_policy_basic_v1_t policy[] = {
      {ZX_POL_NEW_EVENT, ZX_POL_ACTION_DENY_EXCEPTION},
  };
  CheckInvokingPolicyWithException(policy, static_cast<uint32_t>(fbl::count_of(policy)),
                                   MINIP_CMD_CREATE_EVENT, ZX_ERR_ACCESS_DENIED);
}

TEST(JobPolicyTest, TestExceptionOnNewEventButAllow) {
  zx_policy_basic_v1_t policy[] = {
      {ZX_POL_NEW_EVENT, ZX_POL_ACTION_ALLOW_EXCEPTION},
  };
  CheckInvokingPolicyWithException(policy, static_cast<uint32_t>(fbl::count_of(policy)),
                                   MINIP_CMD_CREATE_EVENT, ZX_OK);
}

TEST(JobPolicyTest, TestExceptionOnNewProfileAndDeny) {
  zx_policy_basic_v1_t policy[] = {
      {ZX_POL_NEW_PROFILE, ZX_POL_ACTION_DENY_EXCEPTION},
  };
  CheckInvokingPolicyWithException(policy, static_cast<uint32_t>(fbl::count_of(policy)),
                                   MINIP_CMD_CREATE_PROFILE, ZX_ERR_ACCESS_DENIED);
}

// Test ZX_POL_BAD_HANDLE when syscalls are allowed to continue.
TEST(JobPolicyTest, TestErrorOnBadHandle) {
  // The ALLOW and DENY actions should be equivalent for ZX_POL_BAD_HANDLE.
  uint32_t actions[] = {ZX_POL_ACTION_ALLOW, ZX_POL_ACTION_DENY};
  for (uint32_t action : actions) {
    zx_policy_basic_v1_t policy[] = {
        {ZX_POL_BAD_HANDLE, action},
    };
    CheckInvokingPolicy(policy, static_cast<uint32_t>(fbl::count_of(policy)),
                        MINIP_CMD_USE_BAD_HANDLE_CLOSED, ZX_ERR_BAD_HANDLE);
    CheckInvokingPolicy(policy, static_cast<uint32_t>(fbl::count_of(policy)),
                        MINIP_CMD_USE_BAD_HANDLE_TRANSFERRED, ZX_ERR_BAD_HANDLE);
  }
}

// Test ZX_POL_BAD_HANDLE with ZX_POL_ACTION_EXCEPTION.
TEST(JobPolicyTest, TestExceptionOnBadHandle) {
  // The ALLOW_EXCEPTION and DENY_EXCEPTION actions should be equivalent for ZX_POL_BAD_HANDLE.
  uint32_t actions[] = {ZX_POL_ACTION_ALLOW_EXCEPTION, ZX_POL_ACTION_DENY_EXCEPTION};
  for (uint32_t action : actions) {
    zx_policy_basic_v1_t policy[] = {{ZX_POL_BAD_HANDLE, action}};
    CheckInvokingPolicyWithException(policy, static_cast<uint32_t>(fbl::count_of(policy)),
                                     MINIP_CMD_USE_BAD_HANDLE_CLOSED, ZX_ERR_BAD_HANDLE);
    CheckInvokingPolicyWithException(policy, static_cast<uint32_t>(fbl::count_of(policy)),
                                     MINIP_CMD_USE_BAD_HANDLE_TRANSFERRED, ZX_ERR_BAD_HANDLE);
  }
}

// The one exception for ZX_POL_BAD_HANDLE is zx_object_info( ZX_INFO_HANDLE_VALID).
TEST(JobPolicyTest, TestGetInfoOnBadHandle) {
  zx_policy_basic_v1_t policy[] = {{ZX_POL_BAD_HANDLE, ZX_POL_ACTION_DENY_EXCEPTION}};
  CheckInvokingPolicy(policy, static_cast<uint32_t>(fbl::count_of(policy)),
                      MINIP_CMD_VALIDATE_CLOSED_HANDLE, ZX_ERR_BAD_HANDLE);
}

}  // anonymous namespace
