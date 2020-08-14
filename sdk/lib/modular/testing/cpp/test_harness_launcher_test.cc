// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/test/harness/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/modular/testing/cpp/test_harness_launcher.h>
#include <lib/sys/cpp/testing/fake_launcher.h>
#include <zircon/assert.h>

namespace {
constexpr char kTestHarnessUrl[] =
    "fuchsia-pkg://fuchsia.com/modular_test_harness#meta/"
    "modular_test_harness.cmx";
}  // namespace

class FakeController : public fuchsia::sys::ComponentController {
 public:
  FakeController(fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl)
      : loop_(&kAsyncLoopConfigNoAttachToCurrentThread), binding_(this) {
    loop_.StartThread();
    binding_.Bind(std::move(ctrl), loop_.dispatcher());
  }

  ~FakeController() {
    loop_.Quit();
    loop_.JoinThreads();
  }

  bool is_bound() { return binding_.is_bound(); }

 private:
  // |fuchsia::sys::ComponentController|
  void Kill() override { binding_.Unbind(); }

  // |fuchsia::sys::ComponentController|
  void Detach() override {
    // Unimplemented.
    ZX_ASSERT(false);
  }

  async::Loop loop_;
  fidl::Binding<fuchsia::sys::ComponentController> binding_;
};

class TestHarnessLauncherTest : public gtest::RealLoopFixture {
 public:
  TestHarnessLauncherTest() {
    fake_launcher_.RegisterComponent(
        kTestHarnessUrl, [this](fuchsia::sys::LaunchInfo info,
                                fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
          fake_ctrl_ = std::make_unique<FakeController>(std::move(ctrl));
        });
  }

 protected:
  fuchsia::sys::LauncherPtr GetTestHarnessLauncher() {
    fuchsia::sys::LauncherPtr launcher;
    fake_launcher_.GetHandler()(launcher.NewRequest());
    return launcher;
  }

  bool is_running() { return fake_ctrl_ && fake_ctrl_->is_bound(); }

 private:
  sys::testing::FakeLauncher fake_launcher_;
  std::unique_ptr<FakeController> fake_ctrl_;
};

// Test that the TestHarnessLauncher is able to launch modular_test_harness.cmx.
// TODO(fxbug.dev/37263): re-enable once flake is resolved.
TEST_F(TestHarnessLauncherTest, CanLaunchTestHarness) {
  modular_testing::TestHarnessLauncher launcher(GetTestHarnessLauncher());
  RunLoopUntil([this] { return is_running(); });
}

// Test that TestHarnessLauncher will destroy the modular_test_harness.cmx
// component before the destructor returns.
TEST_F(TestHarnessLauncherTest, CleanupInDestructor) {
  // Test that modular_test_harness.cmx is not running.
  {
    modular_testing::TestHarnessLauncher launcher(GetTestHarnessLauncher());
    RunLoopUntil([this] { return is_running(); });
  }
  // Test that the modular_test_harness.cmx is no longer running after
  // TestHarnessLauncher is destroyed.
  EXPECT_FALSE(is_running());
}
