// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/app_driver/cpp/app_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>

#include <peridot/lib/testing/session_shell_base.h>

namespace {

// Implementation of a minimal session shell used for testing purposes. This
// session shell listens for new stories and starts them if they are in a
// stopped state. Note that stopping a running story will cause it to start up
// again.
class TestSessionShellApp : public modular::testing::SessionShellBase,
                            public fuchsia::modular::StoryProviderWatcher {
 public:
  explicit TestSessionShellApp(component::StartupContext* const startup_context)
      : SessionShellBase(startup_context), story_provider_watcher_(this) {
    story_provider()->GetStories(
        story_provider_watcher_.NewBinding(),
        [](std::vector<fuchsia::modular::StoryInfo>) {});
  }

  virtual ~TestSessionShellApp() override = default;

  // move-only
  TestSessionShellApp(const TestSessionShellApp&) = delete;
  void operator=(const TestSessionShellApp&) = delete;

 private:
  // |fuchsia::modular::StoryProviderWatcher|
  void OnChange(
      fuchsia::modular::StoryInfo story_info,
      fuchsia::modular::StoryState story_state,
      fuchsia::modular::StoryVisibilityState story_visibility_state) override {
    if (story_state == fuchsia::modular::StoryState::STOPPED) {
      fuchsia::modular::StoryControllerPtr story_controller;
      story_provider()->GetController(story_info.id,
                                      story_controller.NewRequest());
      story_controller->RequestStart();
    }
  }

  // |fuchsia::modular::StoryProviderWatcher|
  void OnDelete(std::string story_id) override {}

  fidl::Binding<StoryProviderWatcher> story_provider_watcher_;
};

}  // namespace

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  auto context = component::StartupContext::CreateFromStartupInfo();
  modular::AppDriver<TestSessionShellApp> driver(
      context->outgoing().deprecated_services(),
      std::make_unique<TestSessionShellApp>(context.get()),
      [&loop] { loop.Quit(); });

  loop.Run();
  return 0;
}
