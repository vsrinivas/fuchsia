// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/testing/cpp/fidl.h>

#include <gmock/gmock.h>

#include "lib/syslog/cpp/macros.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_module.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_session_shell.h"
#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"

using testing::ElementsAre;

namespace {

class ModuleContextTest : public modular_testing::TestHarnessFixture {
 protected:
  ModuleContextTest()
      : session_shell_(modular_testing::FakeSessionShell::CreateWithDefaultOptions()) {}

  void StartSession(modular_testing::TestHarnessBuilder builder) {
    builder.InterceptSessionShell(session_shell_->BuildInterceptOptions());
    builder.BuildAndRun(test_harness());

    // Wait for our session shell to start.
    RunLoopUntil([this] { return session_shell_->is_running(); });
  }

  void RestartStory(std::string story_name) {
    fuchsia::modular::StoryControllerPtr story_controller;
    session_shell_->story_provider()->GetController(story_name, story_controller.NewRequest());

    bool restarted = false;
    story_controller->Stop([&] {
      story_controller->RequestStart();
      restarted = true;
    });
    RunLoopUntil([&] { return restarted; });
  }

  std::unique_ptr<modular_testing::FakeSessionShell> session_shell_;
};

// A version of FakeModule which captures handled intents in a std::vector<>
// and exposes callbacks triggered on certain lifecycle events.
class TestModule : public modular_testing::FakeModule {
 public:
  explicit TestModule(std::string module_name = "")
      : modular_testing::FakeModule(
            {.url = modular_testing::TestHarnessBuilder::GenerateFakeUrl(module_name),
             .sandbox_services = modular_testing::FakeModule::GetDefaultSandboxServices()}) {}
  fit::function<void()> on_destroy;
  fit::function<void()> on_create;

 private:
  // |modular_testing::FakeModule|
  void OnCreate(fuchsia::sys::StartupInfo startup_info) override {
    modular_testing::FakeModule::OnCreate(std::move(startup_info));
    if (on_create)
      on_create();
  }

  // |modular_testing::FakeModule|
  void OnDestroy() override {
    if (on_destroy)
      on_destroy();
  }
};

class TestStoryWatcher : fuchsia::modular::StoryWatcher {
 public:
  TestStoryWatcher(fit::function<void(fuchsia::modular::StoryState)> on_state_change)
      : binding_(this), on_state_change_(std::move(on_state_change)) {}
  ~TestStoryWatcher() override = default;

  // Registers itself as a watcher on the given story. Only one story at a time
  // can be watched.
  void Watch(fuchsia::modular::StoryController* const story_controller) {
    story_controller->Watch(binding_.NewBinding());
  }

 private:
  // |fuchsia::modular::StoryWatcher|
  void OnStateChange(fuchsia::modular::StoryState state) override { on_state_change_(state); }
  void OnModuleAdded(fuchsia::modular::ModuleData /*module_data*/) override {}
  void OnModuleFocused(std::vector<std::string> /*module_path*/) override {}

  fidl::Binding<fuchsia::modular::StoryWatcher> binding_;
  fit::function<void(fuchsia::modular::StoryState)> on_state_change_;
};

// Test that ModuleContext.RemoveSelfFromStory() on the only mod in a story has
// the affect of shutting down the module and removing it permanently from the
// story (if the story is restarted, it is not relaunched).
TEST_F(ModuleContextTest, RemoveSelfFromStory) {
  TestModule module1("module1");

  modular_testing::TestHarnessBuilder builder;
  builder.InterceptComponent(module1.BuildInterceptOptions());

  StartSession(std::move(builder));
  modular_testing::AddModToStory(test_harness(), "storyname", "modname1",
                                 {.action = "action", .handler = module1.url()});
  RunLoopUntil([&] { return module1.is_running(); });

  // Instruct module1 to remove itself from the story. Expect to see that
  // module1 is terminated.
  module1.module_context()->RemoveSelfFromStory();
  RunLoopUntil([&] { return !module1.is_running(); });

  // Additionally, restarting the story should not result in module1 being
  // restarted.
  fuchsia::modular::StoryControllerPtr story_controller;
  session_shell_->story_provider()->GetController("storyname", story_controller.NewRequest());
  bool story_stopped = false;
  bool story_restarted = false;
  TestStoryWatcher story_watcher([&](fuchsia::modular::StoryState state) {
    if (state == fuchsia::modular::StoryState::STOPPED) {
      story_stopped = true;
    } else if (state == fuchsia::modular::StoryState::RUNNING) {
      story_restarted = true;
    }
  });
  story_watcher.Watch(story_controller.get());
  RestartStory("storyname");
  RunLoopUntil([&] { return story_stopped && story_restarted; });
  EXPECT_FALSE(module1.is_running());
}

// Test that when ModuleContext.RemoveSelfFromStory() is called on one of two
// modules in a story, it has the affect of shutting down the module and
// removing it permanently from the story (if the story is restarted, it is not
// relaunched).
TEST_F(ModuleContextTest, RemoveSelfFromStory_2mods) {
  modular_testing::TestHarnessBuilder builder;

  TestModule module1("module1");
  TestModule module2("module2");
  builder.InterceptComponent(module1.BuildInterceptOptions());
  builder.InterceptComponent(module2.BuildInterceptOptions());

  StartSession(std::move(builder));
  modular_testing::AddModToStory(test_harness(), "storyname", "modname1",
                                 {.action = "action", .handler = module1.url()});
  modular_testing::AddModToStory(test_harness(), "storyname", "modname2",
                                 {.action = "action", .handler = module2.url()});
  RunLoopUntil([&] { return module1.is_running() && module2.is_running(); });

  // Instruct module1 to remove itself from the story. Expect to see that
  // module1 is terminated and module2 is not.
  module1.module_context()->RemoveSelfFromStory();
  RunLoopUntil([&] { return !module1.is_running(); });
  ASSERT_TRUE(module2.is_running());

  // Additionally, restarting the story should not result in module1 being
  // restarted whereas it should for module2.
  bool module2_destroyed = false;
  bool module2_restarted = false;
  module2.on_destroy = [&] { module2_destroyed = true; };
  module2.on_create = [&] { module2_restarted = true; };
  RestartStory("storyname");
  RunLoopUntil([&] { return module2_restarted; });
  EXPECT_FALSE(module1.is_running());
  EXPECT_TRUE(module2_destroyed);
}

}  // namespace
