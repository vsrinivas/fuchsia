// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/modular_test_harness/cpp/test_harness_fixture.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/testing/test_with_environment.h>

class TestHarnessFixtureTest : public modular::testing::TestHarnessFixture {};

// Test that InterceptBaseShell() generates a base shell URL and sets it up for
// interception.
TEST_F(TestHarnessFixtureTest, InterceptBaseShell) {
  fuchsia::modular::testing::TestHarnessSpec spec;
  auto url = InterceptBaseShell(&spec);
  EXPECT_FALSE(url.empty());
  EXPECT_EQ(url, spec.basemgr_config().base_shell().app_config().url());
}

// Test that InterceptSessionShell() generates a new session shell URL and sets
// it up for interception.
TEST_F(TestHarnessFixtureTest, InterceptSessionShell) {
  fuchsia::modular::testing::TestHarnessSpec spec;
  auto url = InterceptSessionShell(&spec);
  EXPECT_FALSE(url.empty());
  EXPECT_EQ(url, spec.basemgr_config()
                     .session_shell_map()
                     .at(0)
                     .config()
                     .app_config()
                     .url());
  EXPECT_EQ(url, spec.components_to_intercept().at(0).component_url());
}

// Test that InterceptStoryShell() generates a story shell URL and sets it up
// for interception.
TEST_F(TestHarnessFixtureTest, InterceptStoryShell) {
  fuchsia::modular::testing::TestHarnessSpec spec;
  auto url = InterceptStoryShell(&spec);
  EXPECT_FALSE(url.empty());
  EXPECT_EQ(url, spec.basemgr_config().story_shell().app_config().url());
  EXPECT_EQ(url, spec.components_to_intercept().at(0).component_url());
}

// Test that GenerateFakeUrl() returns new urls each time.
TEST_F(TestHarnessFixtureTest, GenerateFakeUrl) {
  EXPECT_NE(GenerateFakeUrl(), GenerateFakeUrl());
}

// Test that the TestHarnessFixture is able to launch the modular runtime by
// asserting that we can intercept a base shell.
TEST_F(TestHarnessFixtureTest, SimpleSuccess) {
  constexpr char kFakeBaseShellUrl[] =
      "fuchsia-pkg://example.com/FAKE_BASE_SHELL_PKG/fake_base_shell.cmx";

  // Setup base shell interception.
  fuchsia::modular::testing::InterceptSpec shell_intercept_spec;
  shell_intercept_spec.set_component_url(kFakeBaseShellUrl);

  fuchsia::modular::testing::TestHarnessSpec spec;
  spec.mutable_basemgr_config()
      ->mutable_base_shell()
      ->mutable_app_config()
      ->set_url(kFakeBaseShellUrl);
  spec.mutable_components_to_intercept()->push_back(
      std::move(shell_intercept_spec));

  // Listen for base shell interception.
  bool intercepted = false;

  test_harness().events().OnNewComponent =
      [&](fuchsia::sys::StartupInfo startup_info,
          fidl::InterfaceHandle<fuchsia::modular::testing::InterceptedComponent>
              component) {
        ASSERT_EQ(kFakeBaseShellUrl, startup_info.launch_info.url);
        intercepted = true;
      };

  test_harness()->Run(std::move(spec));

  ASSERT_TRUE(RunLoopUntil([&] { return intercepted; }));
}
