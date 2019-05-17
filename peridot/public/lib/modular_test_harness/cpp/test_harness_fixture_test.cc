// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/fsl/vmo/strings.h>
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

// Test that the TestHarnessBuilder builds a sane TestHarnessSpec and
// OnNewComponent router function.
TEST_F(TestHarnessFixtureTest, TestHarnessBuilderTest) {
  modular::testing::TestHarnessBuilder builder;

  std::string called;
  builder.InterceptComponent(
      [&](auto launch_info, auto handle) { called = "generic"; },
      {.url = "generic", .sandbox_services = {"library.Protocol"}});
  builder.InterceptBaseShell(
      [&](auto launch_info, auto handle) { called = "base_shell"; },
      {.url = "base_shell"});
  builder.InterceptSessionShell(
      [&](auto launch_info, auto handle) { called = "session_shell"; },
      {.url = "session_shell"});
  builder.InterceptStoryShell(
      [&](auto launch_info, auto handle) { called = "story_shell"; },
      {.url = "story_shell"});

  auto spec = builder.BuildSpec();
  EXPECT_EQ("generic", spec.components_to_intercept().at(0).component_url());
  ASSERT_TRUE(spec.components_to_intercept().at(0).has_extra_cmx_contents());
  std::string cmx_str;
  ASSERT_TRUE(fsl::StringFromVmo(
      spec.components_to_intercept().at(0).extra_cmx_contents(), &cmx_str));
  EXPECT_EQ(R"({"sandbox":{"services":["library.Protocol"]}})", cmx_str);
  EXPECT_EQ("base_shell", spec.components_to_intercept().at(1).component_url());
  EXPECT_EQ("session_shell",
            spec.components_to_intercept().at(2).component_url());
  EXPECT_EQ("story_shell",
            spec.components_to_intercept().at(3).component_url());

  EXPECT_EQ("base_shell",
            spec.basemgr_config().base_shell().app_config().url());
  EXPECT_EQ("session_shell", spec.basemgr_config()
                                 .session_shell_map()
                                 .at(0)
                                 .config()
                                 .app_config()
                                 .url());
  EXPECT_EQ("story_shell",
            spec.basemgr_config().story_shell().app_config().url());

  auto handler = builder.BuildOnNewComponentHandler();
  {
    fuchsia::sys::StartupInfo startup_info;
    startup_info.launch_info.url = "generic";
    handler(std::move(startup_info), nullptr);
    EXPECT_EQ("generic", called);
  }
  {
    fuchsia::sys::StartupInfo startup_info;
    startup_info.launch_info.url = "base_shell";
    handler(std::move(startup_info), nullptr);
    EXPECT_EQ("base_shell", called);
  }
  {
    fuchsia::sys::StartupInfo startup_info;
    startup_info.launch_info.url = "session_shell";
    handler(std::move(startup_info), nullptr);
    EXPECT_EQ("session_shell", called);
  }
  {
    fuchsia::sys::StartupInfo startup_info;
    startup_info.launch_info.url = "story_shell";
    handler(std::move(startup_info), nullptr);
    EXPECT_EQ("story_shell", called);
  }
}

// Test that GenerateFakeUrl() returns new urls each time.
TEST_F(TestHarnessFixtureTest, GenerateFakeUrl) {
  modular::testing::TestHarnessBuilder builder;
  EXPECT_NE(builder.GenerateFakeUrl(), builder.GenerateFakeUrl());
}

// Test that the TestHarnessFixture is able to launch the modular runtime by
// asserting that we can intercept a base shell.
TEST_F(TestHarnessFixtureTest, SimpleSuccess) {
  constexpr char kFakeBaseShellUrl[] =
      "fuchsia-pkg://example.com/FAKE_BASE_SHELL_PKG/fake_base_shell.cmx";

  // Setup base shell interception.
  modular::testing::TestHarnessBuilder builder;

  bool intercepted = false;
  builder.InterceptBaseShell(
      [&](fuchsia::sys::StartupInfo startup_info,
          fidl::InterfaceHandle<fuchsia::modular::testing::InterceptedComponent>
              component) {
        ASSERT_EQ(kFakeBaseShellUrl, startup_info.launch_info.url);
        intercepted = true;
      },
      {.url = kFakeBaseShellUrl});

  test_harness().events().OnNewComponent = builder.BuildOnNewComponentHandler();
  test_harness()->Run(builder.BuildSpec());
  RunLoopUntil([&] { return intercepted; });
}
