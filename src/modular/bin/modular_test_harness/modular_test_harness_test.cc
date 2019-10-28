// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/testing/cpp/fidl.h>

#include <sdk/lib/modular/testing/cpp/test_harness_builder.h>
#include <sdk/lib/modular/testing/cpp/test_harness_launcher.h>
#include <sdk/lib/sys/cpp/service_directory.h>
#include <sdk/lib/sys/cpp/testing/test_with_environment.h>
#include <src/modular/lib/modular_test_harness/cpp/fake_session_shell.h>
#include <src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h>

#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"

class ModularTestHarnessTest : public modular_testing::TestHarnessFixture {};

// Ensure that the TestHarnessFixture is able to launch the modular runtime.
TEST_F(ModularTestHarnessTest, SimpleSuccess) {
  constexpr char kFakeBaseShellUrl[] =
      "fuchsia-pkg://example.com/FAKE_BASE_SHELL_PKG/fake_base_shell.cmx";

  // Setup base shell interception.
  fuchsia::modular::testing::InterceptSpec shell_intercept_spec;
  shell_intercept_spec.set_component_url(kFakeBaseShellUrl);

  fuchsia::modular::testing::TestHarnessSpec spec;
  spec.mutable_basemgr_config()->mutable_base_shell()->mutable_app_config()->set_url(
      kFakeBaseShellUrl);
  spec.mutable_components_to_intercept()->push_back(std::move(shell_intercept_spec));

  // Listen for base shell interception.
  bool intercepted = false;

  test_harness().events().OnNewComponent =
      [&](fuchsia::sys::StartupInfo startup_info,
          fidl::InterfaceHandle<fuchsia::modular::testing::InterceptedComponent> component) {
        ASSERT_EQ(kFakeBaseShellUrl, startup_info.launch_info.url);
        intercepted = true;
      };

  test_harness()->Run(std::move(spec));

  RunLoopUntil([&] { return intercepted; });
}

class ModularTestHarnessDestructionTest : public sys::testing::TestWithEnvironment,
                                          protected modular_testing::FakeSessionShell {
 protected:
  ModularTestHarnessDestructionTest()
      : FakeSessionShell({.url = modular_testing::TestHarnessBuilder::GenerateFakeUrl(),
                          .sandbox_services = {
                              "fuchsia.modular.SessionShellContext",
                          }}) {
    termination_signaled_.store(false);
  }

  std::atomic<bool> termination_signaled_;

 private:
  // |fuchsia::modular::Lifecycle|
  void Terminate() override {
    termination_signaled_.store(true);
    modular_testing::FakeSessionShell::Terminate();
  };
};

// Test that a session is torn down cleanly.
TEST_F(ModularTestHarnessDestructionTest, DISABLED_CleanTeardown) {
  modular_testing::TestHarnessBuilder builder;

  // Service our FakeSessionShell on a different thread -- this will allow our FakeSessionShell to
  // respond to Lifecycle/Terminate() while TestHarnessLauncher blocks this thread.
  async::Loop session_shell_loop(&kAsyncLoopConfigNoAttachToThread);
  session_shell_loop.StartThread();
  {
    modular_testing::TestHarnessLauncher launcher(
        real_services()->Connect<fuchsia::sys::Launcher>());
    builder.InterceptSessionShell(BuildInterceptOptions(session_shell_loop.dispatcher()));
    builder.BuildAndRun(launcher.test_harness());

    RunLoopUntil([&] { return is_running(); });
  }
  // Check that the session shell received a Lifecycle/Terminate() and wasn't forced killed.
  RunLoopUntil([&] { return termination_signaled_.load(); });

  session_shell_loop.Quit();
  session_shell_loop.JoinThreads();
}
