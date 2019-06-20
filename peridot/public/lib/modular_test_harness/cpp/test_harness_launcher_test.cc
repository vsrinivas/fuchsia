// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/gtest/real_loop_fixture.h>
#include <lib/modular_test_harness/cpp/test_harness_launcher.h>
#include <src/lib/files/glob.h>
#include <test/modular/test/harness/cpp/fidl.h>

namespace {
constexpr char kModularTestHarnessHubPath[] = "/hub/c/modular_test_harness.cmx";
}  // namespace

class TestHarnessLauncherTest : public gtest::RealLoopFixture {};

// Test that the TestHarnessLauncher is able to launch modular_test_harness.cmx.
TEST_F(TestHarnessLauncherTest, CanLaunchTestHarness) {
  modular::testing::TestHarnessLauncher launcher;
  RunLoopUntil(
      [] { return files::Glob(kModularTestHarnessHubPath).size() > 0; });
}

// Test that TestHarnessLauncher will destroy the modular_test_harness.cmx
// component before the destructor returns.
TEST_F(TestHarnessLauncherTest, CleanupInDestructor) {
  // Test that modular_test_harness.cmx is not running.
  {
    modular::testing::TestHarnessLauncher launcher;
    RunLoopUntil(
        [] { return files::Glob(kModularTestHarnessHubPath).size() > 0; });
  }
  // Test that the modular_test_harness.cmx is no longer running after
  // TestHarnessLauncher is destroyed.
  auto exists = files::Glob(kModularTestHarnessHubPath).size() > 0;
  EXPECT_FALSE(exists);
}
