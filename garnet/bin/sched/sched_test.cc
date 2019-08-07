// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sched.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/zx/process.h>
#include <lib/zx/profile.h>
#include <stdio.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <string>

#include <gtest/gtest.h>
#include <src/lib/files/file.h>

namespace sched {
namespace {

// Determine if we have access to the system ProfileProvider.
//
// We should if directly invoked from the command line, but may not if we
// are running in a test harness.
bool IsProfileProviderAvailable() {
  return files::IsFile("/svc/fuchsia.scheduler.ProfileProvider");
}

TEST(SchedTest, TestCreateProfile) {
  if (!IsProfileProviderAvailable()) {
    printf("No ProfileProvider available: Skipping test.\n");
    return;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  zx::profile profile;
  ASSERT_EQ(CreateProfile(10, "test-profile", &profile), ZX_OK);
  ASSERT_NE(profile.get(), ZX_HANDLE_INVALID);
}

TEST(SchedTest, TestApplyProfileSelf) {
  if (!IsProfileProviderAvailable()) {
    printf("No ProfileProvider available: Skipping test.\n");
    return;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  zx::profile profile;
  ASSERT_EQ(CreateProfile(10, "test-profile", &profile), ZX_OK);
  ASSERT_EQ(ApplyProfileToProcess(*zx::process::self(), profile, /*verbose=*/false), ZX_OK);
}

TEST(SchedTest, TestBadLaunch) {
  zx::process process;
  std::string error_message;
  zx_status_t result =
      Launch(ZX_HANDLE_INVALID, {"/this/does/not/exist"}, &process, &error_message);
  EXPECT_EQ(result, ZX_ERR_NOT_FOUND);
  EXPECT_TRUE(!error_message.empty());
}

TEST(SchedTest, TestGoodLaunch) {
  zx::process process;
  std::string error_message;
  zx_status_t result =
      Launch(ZX_HANDLE_INVALID, {"/boot/bin/sh", "-c", "echo success"}, &process, &error_message);
  EXPECT_EQ(result, ZX_OK);
  EXPECT_TRUE(error_message.empty());
}

}  // namespace
}  // namespace sched
