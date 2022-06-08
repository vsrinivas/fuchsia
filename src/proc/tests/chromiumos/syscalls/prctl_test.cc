// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/prctl.h>

#include <gtest/gtest.h>

#include "src/proc/tests/chromiumos/syscalls/test_helper.h"

namespace {
TEST(PrctlTest, SubReaperTest) {
  // Reap children.
  prctl(PR_SET_CHILD_SUBREAPER, 1);

  pid_t ancestor_pid = SAFE_SYSCALL(getpid());
  if (SAFE_SYSCALL(fork()) == 0) {
    // Fork again
    if (SAFE_SYSCALL(fork()) == 0) {
      // Nothing to do. Return and makes the child an orphan.
    } else {
      // Wait to be reparented.
      while (SAFE_SYSCALL(getppid()) != ancestor_pid) {
      }
    }
    // Ensure all forked process will exit and not reach back to gtest.
    exit(0);
  } else {
    // Expect that both child ends up being repated to this process.
    for (size_t i = 0; i < 2; ++i) {
      EXPECT_GT(wait(nullptr), 0);
    }
  }
}
}  // namespace
