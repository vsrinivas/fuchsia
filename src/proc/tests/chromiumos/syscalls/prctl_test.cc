// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/prctl.h>

#include <gtest/gtest.h>
#include <linux/securebits.h>

#include "src/proc/tests/chromiumos/syscalls/test_helper.h"

namespace {

TEST(PrctlTest, SubReaperTest) {
  ForkHelper helper;

  // Reap children.
  prctl(PR_SET_CHILD_SUBREAPER, 1);

  pid_t ancestor_pid = SAFE_SYSCALL(getpid());
  ASSERT_NE(1, ancestor_pid);
  pid_t parent_pid = SAFE_SYSCALL(getppid());
  ASSERT_NE(0, parent_pid);
  ASSERT_NE(ancestor_pid, parent_pid);

  helper.RunInForkedProcess([&] {
    // Fork again
    helper.RunInForkedProcess([&] {
      // Wait to be reparented.
      while (SAFE_SYSCALL(getppid()) != ancestor_pid) {
      }
    });
    // Parent return and makes the child an orphan.
  });

  // Expect that both child ends up being reaped to this process.
  for (size_t i = 0; i < 2; ++i) {
    EXPECT_GT(wait(nullptr), 0);
  }
}

TEST(PrctlTest, SecureBits) {
  ForkHelper helper;

  helper.RunInForkedProcess([&] {
    SAFE_SYSCALL(prctl(PR_SET_SECUREBITS, SECBIT_NOROOT));
    ASSERT_EQ(SAFE_SYSCALL(prctl(PR_GET_SECUREBITS)), SECBIT_NOROOT);
    SAFE_SYSCALL(prctl(PR_SET_SECUREBITS, SECBIT_KEEP_CAPS));
    ASSERT_EQ(SAFE_SYSCALL(prctl(PR_GET_SECUREBITS)), SECBIT_KEEP_CAPS);
  });

  ASSERT_TRUE(helper.WaitForChildren());
}

}  // namespace
