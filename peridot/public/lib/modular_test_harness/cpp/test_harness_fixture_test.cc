// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/modular_test_harness/cpp/test_harness_fixture.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/testing/test_with_environment.h>

class TestHarnessFixtureTest : public modular::testing::TestHarnessFixture {};

// Test that the TestHarnessFixture is able to launch the modular runtime by
// asserting that we can intercept a base shell.
TEST_F(TestHarnessFixtureTest, SimpleSuccess) {
  constexpr char kFakeBaseShellUrl[] =
      "fuchsia-pkg://example.com/FAKE_BASE_SHELL_PKG/fake_base_shell.cmx";

  fuchsia::modular::testing::InterceptSpec shell_intercept_spec;
  shell_intercept_spec.set_component_url(kFakeBaseShellUrl);
  fuchsia::modular::testing::TestHarnessSpec spec;
  spec.mutable_base_shell()->set_intercept_spec(
      std::move(shell_intercept_spec));

  // Listen for base shell interception.
  bool intercepted = false;

  test_harness().events().OnNewBaseShell =
      [&intercepted](
          fuchsia::sys::StartupInfo startup_info,
          fidl::InterfaceHandle<fuchsia::modular::testing::InterceptedComponent>
              component) { intercepted = true; };

  test_harness()->Run(std::move(spec));

  ASSERT_TRUE(
      RunLoopWithTimeoutOrUntil([&] { return intercepted; }, zx::sec(5)));
}
