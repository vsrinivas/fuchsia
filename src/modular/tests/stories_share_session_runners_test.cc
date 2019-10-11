// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/testing/cpp/fidl.h>

#include <sdk/lib/sys/cpp/component_context.h>
#include <src/lib/fxl/logging.h>
#include <src/modular/lib/modular_test_harness/cpp/fake_component.h>
#include <src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h>

namespace {

constexpr char kFakeRunnerUrl[] = "fuchsia-pkg://fuchsia.com/fake_runner#meta/fake_runner.cmx";

// A module that specifies kFakeRunnerUrl as the runner to be used to launch
// itself. The module doesn't have any functionality besides starting up and
// tearing down.
constexpr char kModuleWithFakeRunnerUrl[] =
    "fuchsia-pkg://fuchsia.com/module_with_fake_runner#meta/module_with_fake_runner.cmx";

// A runner that is used to count the number of times it is launched, when starting
// multiple instances of the same module in different stories.
class FakeRunner : public modular_testing::FakeComponent, fuchsia::sys::Runner {
 public:
  FakeRunner() : FakeComponent({.url = kFakeRunnerUrl}) {}

  int module_started_count() { return module_started_count_; }

 protected:
  void OnCreate(fuchsia::sys::StartupInfo startup_info) override {
    runner_intercepted_count_++;
    EXPECT_EQ(1, runner_intercepted_count_);
    component_context()->outgoing()->AddPublicService<fuchsia::sys::Runner>(
        [this](fidl::InterfaceRequest<fuchsia::sys::Runner> request) {
          bindings_.AddBinding(this, std::move(request));
        });
  }

  // |fuchsia::sys::Runner|
  void StartComponent(
      fuchsia::sys::Package package, fuchsia::sys::StartupInfo startup_info,
      ::fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller) override {
    module_started_count_++;
  }

  fidl::BindingSet<fuchsia::sys::Runner> bindings_;
  int runner_intercepted_count_ = 0;
  int module_started_count_ = 0;
};

class StoriesShareSessionRunnersTest : public modular_testing::TestHarnessFixture {
 protected:
  void SetUp() override {
    builder_.InterceptComponent(
        {.url = kFakeRunnerUrl,
         .sandbox_services = {},
         .launch_handler =
             [this](fuchsia::sys::StartupInfo startup_info,
                    fidl::InterfaceHandle<fuchsia::modular::testing::InterceptedComponent>
                        intercepted_component) mutable {
               runners_requested_++;
               // This test should ensure the fake_runner_ is requested only once, no matter how
               // many stories or modules request it.
               EXPECT_EQ(fake_runner_, nullptr);  // don't ASSERT from void lambda
               if (fake_runner_ != nullptr) {
                 // If unexpected second request, create a new runner so module construction will
                 // succeed in the new story, but fail the test based on |runners_requested_| > 1.
                 // (First, move the original runner so it isn't destructed on unique_ptr
                 // overwrite.)
                 saved_runner_ = std::move(fake_runner_);
               }
               fake_runner_ = std::make_unique<FakeRunner>();
               fake_runner_->BuildInterceptOptions().launch_handler(
                   std::move(startup_info), std::move(intercepted_component));
             }});
    builder_.BuildAndRun(test_harness());
  }

  std::unique_ptr<FakeRunner> fake_runner_;
  std::unique_ptr<FakeRunner> saved_runner_;
  int runners_requested_ = 0;
  modular_testing::TestHarnessBuilder builder_;
};

// Tests that the same mod started in different stories will reuse the
// runner started by the first mod because stories share the same environment.
TEST_F(StoriesShareSessionRunnersTest, ModReusesRunner) {
  auto first_intent = fuchsia::modular::Intent{.handler = kModuleWithFakeRunnerUrl,
                                               .action = "com.google.fuchsia.module.runner"};

  // Add a mod that will be launched via a fake runner
  modular_testing::AddModToStory(test_harness(), "first_story", "mod_name",
                                 std::move(first_intent));

  RunLoopUntil([&] { return !!fake_runner_ && fake_runner_->module_started_count() > 0; });
  EXPECT_EQ(1, runners_requested_);
  EXPECT_EQ(1, fake_runner_->module_started_count());

  // Add the same mod and check that the runner wasn't launched again
  auto second_intent = fuchsia::modular::Intent{.handler = kModuleWithFakeRunnerUrl,
                                                .action = "com.google.fuchsia.module.runner"};
  modular_testing::AddModToStory(test_harness(), "second_story", "mod_name",
                                 std::move(second_intent));
  RunLoopUntil([&] {
    return fake_runner_->module_started_count() > 1 ||
           (runners_requested_ > 1 && !!fake_runner_ && fake_runner_->module_started_count() > 0);
  });
  EXPECT_EQ(2, fake_runner_->module_started_count());
  ASSERT_EQ(1, runners_requested_);

  // Add the same mod and check that the runner wasn't launched again
  auto third_intent = fuchsia::modular::Intent{.handler = kModuleWithFakeRunnerUrl,
                                               .action = "com.google.fuchsia.module.runner"};
  modular_testing::AddModToStory(test_harness(), "second_story", "mod_name_2_of_2",
                                 std::move(third_intent));
  RunLoopUntil([&] {
    return fake_runner_->module_started_count() > 2 ||
           (runners_requested_ > 1 && !!fake_runner_ && fake_runner_->module_started_count() > 0);
  });
  EXPECT_EQ(3, fake_runner_->module_started_count());
  ASSERT_EQ(1, runners_requested_);

  // Add the same mod and check that the runner wasn't launched again
  auto fourth_intent = fuchsia::modular::Intent{.handler = kModuleWithFakeRunnerUrl,
                                                .action = "com.google.fuchsia.module.runner"};
  modular_testing::AddModToStory(test_harness(), "third_story", "mod_name",
                                 std::move(fourth_intent));
  RunLoopUntil([&] {
    return fake_runner_->module_started_count() > 3 ||
           (runners_requested_ > 1 && !!fake_runner_ && fake_runner_->module_started_count() > 0);
  });
  EXPECT_EQ(4, fake_runner_->module_started_count());
  ASSERT_EQ(1, runners_requested_);
}

}  // namespace
