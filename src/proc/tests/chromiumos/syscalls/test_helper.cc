// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/proc/tests/chromiumos/syscalls/test_helper.h"

#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <gtest/gtest.h>

namespace {
::testing::AssertionResult WaitForChildrenInternal() {
  ::testing::AssertionResult result = ::testing::AssertionSuccess();
  for (;;) {
    int wstatus;
    if (wait(&wstatus) == -1) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == ECHILD) {
        // No more child, reaping is done.
        return result;
      }
      // Another error is unexpected.
      result = ::testing::AssertionFailure()
               << "wait error: " << strerror(errno) << "(" << errno << ")";
    }
    if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0) {
      result = ::testing::AssertionFailure()
               << "wait_status: WIFEXITED(wstatus) = " << WIFEXITED(wstatus)
               << ", WEXITSTATUS(wstatus) = " << WEXITSTATUS(wstatus)
               << ", WTERMSIG(wstatus) = " << WTERMSIG(wstatus);
    }
  }
}

}  // namespace

ForkHelper::ForkHelper() {
  // Ensure that all children will ends up being parented to the process that
  // created the helper.
  prctl(PR_SET_CHILD_SUBREAPER, 1);
}

ForkHelper::~ForkHelper() {
  // Wait for all remaining children, and ensure non failed.
  EXPECT_TRUE(WaitForChildrenInternal()) << ": at least a child had a failure";
}

bool ForkHelper::WaitForChildren() { return WaitForChildrenInternal(); }

pid_t ForkHelper::RunInForkedProcess(std::function<void()> action) {
  pid_t pid = SAFE_SYSCALL(fork());
  if (pid != 0) {
    return pid;
  }
  action();
  _exit(testing::Test::HasFailure());
}
