// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/testing/cpp/fidl.h>

#include "gmock/gmock.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_session_shell.h"
#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"
#include "src/modular/lib/testing/session_shell_impl.h"

using testing::ElementsAre;
using testing::Gt;

namespace {

class LastFocusTimeTest : public modular_testing::TestHarnessFixture {};

// A simple story provider watcher implementation. It confirms that it sees an
// increase in the last_focus_time in the fuchsia::modular::StoryInfo it
// receives, and pushes the test through to the next step.
class TestStoryProviderWatcher : public fuchsia::modular::StoryProviderWatcher {
 public:
  TestStoryProviderWatcher() : binding_(this) {}
  ~TestStoryProviderWatcher() override = default;

  void OnChange2(fit::function<void(fuchsia::modular::StoryInfo2)> on_change) {
    on_change_2_ = std::move(on_change);
  }

  void Watch(fuchsia::modular::StoryProvider* const story_provider) {
    story_provider->Watch(binding_.NewBinding());
  }

 private:
  // |fuchsia::modular::StoryProviderWatcher|
  void OnDelete(::std::string story_id) override {}

  // |fuchsia::modular::StoryProviderWatcher|
  void OnChange2(fuchsia::modular::StoryInfo2 story_info, fuchsia::modular::StoryState story_state,
                 fuchsia::modular::StoryVisibilityState story_visibility_state) override {
    on_change_2_(std::move(story_info));
    return;
  }

  fit::function<void(fuchsia::modular::StoryInfo2)> on_change_2_;
  fidl::Binding<fuchsia::modular::StoryProviderWatcher> binding_;
};

class TestStoryWatcher : fuchsia::modular::StoryWatcher {
 public:
  TestStoryWatcher() : binding_(this) {}
  ~TestStoryWatcher() override = default;

  // Registers itself as a watcher on the given story. Only one story at a time
  // can be watched.
  void Watch(fuchsia::modular::StoryController* const story_controller) {
    story_controller->Watch(binding_.NewBinding());
  }

  // Sets the function where to continue when the story is observed to be
  // running.
  void OnStoryRunning(fit::function<void()> at) { continue_ = std::move(at); }

 private:
  // |fuchsia::modular::StoryWatcher|
  void OnStateChange(fuchsia::modular::StoryState state) override {
    if (state != fuchsia::modular::StoryState::RUNNING) {
      return;
    }

    continue_();
  }

  // |fuchsia::modular::StoryWatcher|
  void OnModuleAdded(fuchsia::modular::ModuleData /*module_data*/) override {}

  // |fuchsia::modular::StoryWatcher|
  void OnModuleFocused(std::vector<std::string> /*module_path*/) override {}

  fit::function<void()> continue_;
  fidl::Binding<fuchsia::modular::StoryWatcher> binding_;
};
}  // namespace

const char kStoryName[] = "storyname";

TEST_F(LastFocusTimeTest, LastFocusTimeIncreases) {
  modular_testing::TestHarnessBuilder builder;

  auto test_session_shell = modular_testing::FakeSessionShell::CreateWithDefaultOptions();
  builder.InterceptSessionShell(test_session_shell->BuildInterceptOptions());

  // Listen for the module we're going to create.
  modular_testing::FakeComponent test_module(
      {.url = modular_testing::TestHarnessBuilder::GenerateFakeUrl()});
  builder.InterceptComponent(test_module.BuildInterceptOptions());
  builder.BuildAndRun(test_harness());

  // Wait for our session shell to start.
  RunLoopUntil([&] { return test_session_shell->is_running(); });

  fuchsia::modular::FocusControllerPtr focus_controller;
  fuchsia::modular::FocusProviderPtr focus_provider;
  test_session_shell->session_shell_context()->GetFocusController(focus_controller.NewRequest());
  test_session_shell->session_shell_context()->GetFocusProvider(focus_provider.NewRequest());

  // Watch for changes to the session.
  TestStoryProviderWatcher story_provider_watcher;
  story_provider_watcher.Watch(test_session_shell->story_provider());

  // Keep track of the focus timestamps that we receive for the story created
  // below so we can assert that they make sense at the end of the test.
  std::vector<int64_t> last_focus_timestamps;
  story_provider_watcher.OnChange2([&](fuchsia::modular::StoryInfo2 story_info) {
    ASSERT_TRUE(story_info.has_id());
    ASSERT_TRUE(story_info.has_last_focus_time());
    ASSERT_EQ(kStoryName, story_info.id());
    last_focus_timestamps.push_back(story_info.last_focus_time());
  });

  // Create a story so that we can signal the framework to focus it.
  fuchsia::modular::Intent intent;
  intent.handler = test_module.url();
  intent.action = "action";

  modular_testing::AddModToStory(test_harness(), kStoryName, "modname", std::move(intent));

  RunLoopUntil([&] { return test_module.is_running(); });

  // Watch the story and then start it.
  TestStoryWatcher story_watcher;
  fuchsia::modular::StoryControllerPtr story_controller;
  test_session_shell->story_provider()->GetController(kStoryName, story_controller.NewRequest());
  story_watcher.Watch(story_controller.get());
  story_controller->RequestStart();

  story_watcher.OnStoryRunning([&] {
    // Focus the story!
    focus_controller->Set(kStoryName);
  });

  // Run until we have been notified of new last_focus_time values three times.
  // We expect a call for each of:
  // 1) The story is created.
  // 2) The story transitions to running.
  // 3) The story is focused.
  RunLoopUntil([&] { return last_focus_timestamps.size() == 3; });
  EXPECT_THAT(last_focus_timestamps, ElementsAre(0, 0, Gt(0)));
}
