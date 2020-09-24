// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bad-handle.h"

#include <lib/fdio/spawn.h>
#include <lib/zx/event.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls/policy.h>

#include <iterator>

#include <fbl/auto_call.h>
#include <zxtest/zxtest.h>

#include "utils.h"

const char* BadHandleFlagTest() { return "--bad-handle-test"; }

enum class BadHandleTestCase {
  kInvalid,
  kMatchingValueInWait,
  kNonMatchingValueInWait,
  kMatchingValueInRequeue,
  kNonMatchingValueInRequeue,
};

int BadHandleTestMain(int argc, char** argv) {
  zx::event event;
  zx_status_t status = zx::event::create(0, &event);
  if (status != ZX_OK) {
    return 1;
  }

  if (argc != 3) {
    ZX_PANIC("Need to pass an argument");
  }

  BadHandleTestCase test = BadHandleTestCase::kInvalid;
  if (!strcmp(argv[2], "match_wait")) {
    test = BadHandleTestCase::kMatchingValueInWait;
  } else if (!strcmp(argv[2], "no_match_wait")) {
    test = BadHandleTestCase::kNonMatchingValueInWait;
  } else if (!strcmp(argv[2], "match_requeue")) {
    test = BadHandleTestCase::kMatchingValueInRequeue;
  } else if (!strcmp(argv[2], "no_match_requeue")) {
    test = BadHandleTestCase::kNonMatchingValueInRequeue;
  }

  const zx_handle_t bad_handle = event.get() & ~ZX_HANDLE_FIXED_BITS_MASK;

  switch (test) {
    case BadHandleTestCase::kMatchingValueInWait: {
      zx_futex_t futex = 0;
      // This line should cause a BAD_HANDLE policy exception, since the value matches
      return zx_futex_wait(&futex, 0, bad_handle, ZX_TIME_INFINITE);
    }
    case BadHandleTestCase::kNonMatchingValueInWait: {
      zx_futex_t futex = 0;
      // This line should not cause a BAD_HANDLE policy exception, since the value mismatches
      return zx_futex_wait(&futex, 1, bad_handle, ZX_TIME_INFINITE);
    }
    case BadHandleTestCase::kMatchingValueInRequeue: {
      zx_futex_t futex1 = 0, futex2 = 0;
      // This line should cause a BAD_HANDLE policy exception, since the value matches
      return zx_futex_requeue(&futex1, 1, 0, &futex2, 1, bad_handle);
    }
    case BadHandleTestCase::kNonMatchingValueInRequeue: {
      zx_futex_t futex1 = 0, futex2 = 0;
      // This line should not cause a BAD_HANDLE policy exception, since the value mismatches
      return zx_futex_requeue(&futex1, 1, 1, &futex2, 1, bad_handle);
    }
    case BadHandleTestCase::kInvalid:
      break;
  }
  ZX_PANIC("Need to pass a known argument");
}

namespace {

void LaunchTestCase(const char* test_case, zx_info_process_t* proc_info) {
  // Set up a subjob and subprocess in order to set BAD_HANDLE policy.
  zx::job job;
  ASSERT_OK(zx::job::create(*zx::job::default_job(), 0u, &job));
  zx_policy_basic_v2_t policy[] = {{.condition = ZX_POL_BAD_HANDLE,
                                    .action = ZX_POL_ACTION_ALLOW_EXCEPTION,
                                    .flags = ZX_POL_OVERRIDE_DENY}};
  ASSERT_OK(job.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_BASIC_V2, policy,
                           static_cast<uint32_t>(std::size(policy))));

  // Make sure that we have a program name and have not already started.
  ASSERT_NOT_NULL(ExternalThread::ProgramName());

  const char* args[] = {ExternalThread::ProgramName(), BadHandleFlagTest(), test_case, nullptr};
  zx::process proc;
  char err_msg_out[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  zx_status_t res =
      fdio_spawn_etc(job.get(), FDIO_SPAWN_CLONE_ALL, ExternalThread::ProgramName(), args, nullptr,
                     0, nullptr, proc.reset_and_get_address(), err_msg_out);
  ASSERT_OK(res, "%s", err_msg_out);

  res = proc.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr);
  ASSERT_OK(res);

  ASSERT_OK(proc.get_info(ZX_INFO_PROCESS, proc_info, sizeof(*proc_info), nullptr, nullptr));
}

// This is a test for fxbug.dev/41780 where we accidentally enforced the BAD_HANDLE policy in a case
// where the futex value mismatched expectations.
TEST(BadHandleTest, WaitBadHandleWithMismatchedValueDoesNotExit) {
  zx_info_process_t proc_info;
  ASSERT_NO_FATAL_FAILURES(LaunchTestCase("no_match_wait", &proc_info));
  // We should see ZX_ERR_BAD_STATE since the futex value mismatched
  ASSERT_EQ(proc_info.return_code, ZX_ERR_BAD_STATE);
}

TEST(BadHandleTest, WaitBadHandleWithMatchedValueExits) {
  zx_info_process_t proc_info;
  ASSERT_NO_FATAL_FAILURES(LaunchTestCase("match_wait", &proc_info));
  // We should see an exception kill due to the policy violation
  ASSERT_EQ(proc_info.return_code, ZX_TASK_RETCODE_EXCEPTION_KILL);
}

TEST(BadHandleTest, RequeueBadHandleWithMismatchedValueDoesNotExit) {
  zx_info_process_t proc_info;
  ASSERT_NO_FATAL_FAILURES(LaunchTestCase("no_match_requeue", &proc_info));
  // We should see ZX_ERR_BAD_STATE since the futex value mismatched
  ASSERT_EQ(proc_info.return_code, ZX_ERR_BAD_STATE);
}

TEST(BadHandleTest, RequeueBadHandleWithMatchedValueExits) {
  zx_info_process_t proc_info;
  ASSERT_NO_FATAL_FAILURES(LaunchTestCase("match_requeue", &proc_info));
  // We should see an exception kill due to the policy violation
  ASSERT_EQ(proc_info.return_code, ZX_TASK_RETCODE_EXCEPTION_KILL);
}

}  // namespace
