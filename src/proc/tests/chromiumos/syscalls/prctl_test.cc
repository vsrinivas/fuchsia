// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/prctl.h>
#include <sys/syscall.h>

#include <gtest/gtest.h>
#include <linux/capability.h>
#include <linux/prctl.h>
#include <linux/securebits.h>

#include "src/proc/tests/chromiumos/syscalls/test_helper.h"

// These are missing from our sys/prctl.h.
#define PR_CAP_AMBIENT 47
#define PR_CAP_AMBIENT_IS_SET 1
#define PR_CAP_AMBIENT_RAISE 2
#define PR_CAP_AMBIENT_LOWER 3
#define PR_CAP_AMBIENT_CLEAR_ALL 4

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
}

TEST(PrctlTest, DropCapabilities) {
  ForkHelper helper;

  helper.RunInForkedProcess([&] {
    ASSERT_EQ(SAFE_SYSCALL(prctl(PR_CAPBSET_READ, CAP_DAC_OVERRIDE)), true);
    ASSERT_EQ(SAFE_SYSCALL(prctl(PR_CAPBSET_DROP, CAP_DAC_OVERRIDE)), 0);
    ASSERT_EQ(SAFE_SYSCALL(prctl(PR_CAPBSET_READ, CAP_DAC_OVERRIDE)), false);
  });
}

TEST(PrctlTest, CapGet) {
  ForkHelper helper;

  __user_cap_header_struct header;
  memset(&header, 0, sizeof(header));
  header.version = _LINUX_CAPABILITY_VERSION_3;
  __user_cap_data_struct caps[_LINUX_CAPABILITY_U32S_3];
  ASSERT_EQ(syscall(SYS_capget, &header, &caps), 0);

  pid_t parent_pid = getpid();

  helper.RunInForkedProcess([&parent_pid] {
    __user_cap_header_struct header;
    memset(&header, 0, sizeof(header));
    header.version = _LINUX_CAPABILITY_VERSION_3;
    header.pid = parent_pid;
    __user_cap_data_struct caps[_LINUX_CAPABILITY_U32S_3];
    ASSERT_EQ(syscall(SYS_capget, &header, &caps), 0);

    header.pid = 0;
    ASSERT_EQ(syscall(SYS_capset, &header, &caps), 0);

    pid_t child_pid = getpid();
    header.pid = child_pid;
    ASSERT_EQ(syscall(SYS_capset, &header, &caps), 0);

    header.pid = parent_pid;
    ASSERT_EQ(syscall(SYS_capset, &header, &caps), -1);
  });
}

TEST(PrctlTest, AmbientCapabilitiesBasicOperations) {
  ForkHelper helper;

  helper.RunInForkedProcess([&] {
    ASSERT_EQ(SAFE_SYSCALL(prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_LOWER, CAP_CHOWN, 0, 0)), 0);
    ASSERT_EQ(SAFE_SYSCALL(prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, CAP_CHOWN, 0, 0)), 0);

    ASSERT_EQ(SAFE_SYSCALL(prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_CHOWN, 0, 0)), 0);
    ASSERT_EQ(SAFE_SYSCALL(prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, CAP_CHOWN, 0, 0)), 1);

    ASSERT_EQ(SAFE_SYSCALL(prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_CLEAR_ALL, 0, 0, 0)), 0);
    ASSERT_EQ(SAFE_SYSCALL(prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, CAP_CHOWN, 0, 0)), 0);
    ASSERT_EQ(SAFE_SYSCALL(prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, CAP_AUDIT_CONTROL, 0, 0)),
              0);
    ASSERT_EQ(SAFE_SYSCALL(prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, CAP_DAC_OVERRIDE, 0, 0)),
              0);
  });
}

}  // namespace
