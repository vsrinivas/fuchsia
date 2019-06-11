// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/modular_test_harness/cpp/fake_component.h>
#include <lib/modular_test_harness/cpp/test_harness_fixture.h>
#include <sdk/lib/sys/cpp/component_context.h>
#include <src/lib/fxl/logging.h>

namespace {

constexpr char kFakeRunnerUrl[] =
    "fuchsia-pkg://fuchsia.com/fake_runner#meta/fake_runner.cmx";

// A module that specifies kFakeRunnerUrl as the runner to be used to launch
// itself. The module doesn't have any functionality besides starting up and
// tearing down.
constexpr char kModuleWithFakeRunnerUrl[] =
    "fuchsia-pkg://fuchsia.com/module_with_fake_runner#meta/"
    "module_with_fake_runner.cmx";

// A runner that is used to count the number of times its launched when starting
// multiple instances of the same module in different stories.
class FakeRunner : public modular::testing::FakeComponent,
                   fuchsia::sys::Runner {
 public:
  int runner_intercepted_count() { return runner_intercepted_count_; }

 protected:
  void OnCreate(fuchsia::sys::StartupInfo startup_info) override {
    runner_intercepted_count_++;
    component_context()->outgoing()->AddPublicService<fuchsia::sys::Runner>(
        [this](fidl::InterfaceRequest<fuchsia::sys::Runner> request) {
          bindings_.AddBinding(this, std::move(request));
        });
  }

  // |fuchsia::sys::Runner|
  void StartComponent(
      fuchsia::sys::Package package, fuchsia::sys::StartupInfo startup_info,
      ::fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller)
      override {}

  fidl::BindingSet<fuchsia::sys::Runner> bindings_;
  int runner_intercepted_count_ = 0;
};

class StoriesShareSessionRunnersTest
    : public modular::testing::TestHarnessFixture {
 protected:
  void SetUp() override {
    fake_runner_ = std::make_unique<FakeRunner>();
    builder_.InterceptComponent(
        fake_runner_->GetOnCreateHandler(),
        {.url = kFakeRunnerUrl, .sandbox_services = {}});

    test_harness().events().OnNewComponent =
        builder_.BuildOnNewComponentHandler();
    test_harness()->Run(builder_.BuildSpec());
  }

  std::unique_ptr<FakeRunner> fake_runner_;
  modular::testing::TestHarnessBuilder builder_;
};

// Tests that the same mod started in different stories will reuse the
// runner started by the first mod because stories share the same environment.
TEST_F(StoriesShareSessionRunnersTest, ModReusesRunner) {
  auto first_intent =
      fuchsia::modular::Intent{.handler = kModuleWithFakeRunnerUrl,
                               .action = "com.google.fuchsia.module.runner"};

  // Add a mod that will be launched via a fake runner
  AddModToStory(std::move(first_intent), "mod_name", "first_story");

  RunLoopUntil([&] { return fake_runner_->is_running(); });
  EXPECT_EQ(1, fake_runner_->runner_intercepted_count());

  // Add the same mod and check that the runner wasn't launched again
  auto second_intent =
      fuchsia::modular::Intent{.handler = kModuleWithFakeRunnerUrl,
                               .action =
                                   "com.google.fuchsia.module."
                                   "runner"};
  AddModToStory(std::move(second_intent), "mod_name", "second_story");
  EXPECT_EQ(1, fake_runner_->runner_intercepted_count());
}

}  // namespace
