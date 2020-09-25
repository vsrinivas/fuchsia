// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"

#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/modular/testing/cpp/fake_component.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/testing/test_with_environment.h>

#include <gmock/gmock.h>
#include <src/lib/files/glob.h>

#include "src/lib/fsl/vmo/strings.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_module.h"

class TestHarnessFixtureTest : public modular_testing::TestHarnessFixture {};

// Test that the TestHarnessFixture is able to launch the modular runtime by
// asserting that we can intercept a session shell.
TEST_F(TestHarnessFixtureTest, CanLaunchModular) {
  constexpr char kFakeSessionShellUrl[] =
      "fuchsia-pkg://example.com/FAKE_SESSION_SHELL_PKG/fake_session_shell.cmx";

  // Setup session shell interception.
  modular_testing::TestHarnessBuilder builder;

  bool intercepted = false;
  builder.InterceptSessionShell(
      {.url = kFakeSessionShellUrl,
       .launch_handler =
           [&](fuchsia::sys::StartupInfo startup_info,
               fidl::InterfaceHandle<fuchsia::modular::testing::InterceptedComponent> component) {
             ASSERT_EQ(kFakeSessionShellUrl, startup_info.launch_info.url);
             intercepted = true;
           }});
  builder.BuildAndRun(test_harness());

  RunLoopUntil([&] { return intercepted; });
}

TEST_F(TestHarnessFixtureTest, AddModToStory) {
  modular_testing::TestHarnessBuilder builder;

  modular_testing::FakeModule mod({.url = modular_testing::TestHarnessBuilder::GenerateFakeUrl()});
  builder.InterceptComponent(mod.BuildInterceptOptions());
  builder.BuildAndRun(test_harness());

  modular_testing::AddModToStory(test_harness(), "mystory", "mymod",
                                 fuchsia::modular::Intent{.handler = mod.url()});

  RunLoopUntil([&] { return mod.is_running(); });
}

class TestFixtureForTestingCleanup : public modular_testing::TestHarnessFixture {
 public:
  // Runs the test harness and calls |on_running| once the session shell starts
  // running.
  void RunUntilSessionShell(fit::function<void()> on_running) {
    modular_testing::TestHarnessBuilder builder;
    modular_testing::FakeComponent session_shell(
        {.url = modular_testing::TestHarnessBuilder::GenerateFakeUrl()});
    builder.InterceptSessionShell(session_shell.BuildInterceptOptions());
    builder.BuildAndRun(test_harness());

    RunLoopUntil([&] { return session_shell.is_running(); });
    on_running();
  };

  void TearDown() override {
    TestHarnessFixture::TearDown();

    // The test harness launcher should be stopped in |TestHarnessFixture::TearDown|.
    EXPECT_TRUE(!test_harness_launcher()->is_test_harness_running());
  }

  virtual ~TestFixtureForTestingCleanup() = default;

 private:
  // |TestBody()| is usually implemented by gtest's test fixture runner, but
  // since this test is exercising TestHarnessFixture directly, this method is
  // has a dummy implementation here.
  void TestBody() override {}
};

// Test that TestHarnessFixture will destroy the modular_test_harness.cmx
// component in its destructor.
TEST(TestHarnessFixtureCleanupTest, CleanupInDestructor) {
  // Test that modular_test_harness.cmx is not running.
  constexpr char kTestHarnessHubGlob[] = "/hub/c/modular_test_harness.cmx";
  bool exists = files::Glob(kTestHarnessHubGlob).size() == 1;
  EXPECT_FALSE(exists);

  // Test that TestHarnessFixture will run modular_test_harness.cmx
  {
    TestFixtureForTestingCleanup t;
    t.RunUntilSessionShell([&] {
      // check that modular_test_harness.cmx is running.
      bool exists = files::Glob(kTestHarnessHubGlob).size() == 1;
      EXPECT_TRUE(exists);
    });
  }
  // Test that the modular_test_harness.cmx is no longer running after
  // TestHarnessFixture is destroyed.
  exists = files::Glob(kTestHarnessHubGlob).size() == 1;
  EXPECT_FALSE(exists);
}
