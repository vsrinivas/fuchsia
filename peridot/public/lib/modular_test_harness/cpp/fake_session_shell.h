// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MODULAR_TEST_HARNESS_CPP_FAKE_SESSION_SHELL_H_
#define LIB_MODULAR_TEST_HARNESS_CPP_FAKE_SESSION_SHELL_H_

#include <lib/modular_test_harness/cpp/fake_component.h>
#include <sdk/lib/sys/cpp/component_context.h>

#include "peridot/lib/testing/session_shell_impl.h"

namespace modular {
namespace testing {

// Session shell fake that provides access to the StoryProvider, the
// SessionShellContext, and a test implementation of SessionShellImpl.
//
// EXAMPLE USAGE (see test_harness_fixture.h for more details on how to use the
// test harness):
//
// FakeSessionShell fake_session_shell;
// modular::testing::TestHarnessBuilder builder;
// builder.InterceptSessionShell(fake_session_shell.GetOnCreateHandler(),
//                               {.sandbox_services = {
//                                    "fuchsia.modular.SessionShellContext",
//                                    "fuchsia.modular.PuppetMaster"}});
//
// test_harness().events().OnNewComponent =
//     builder.BuildOnNewComponentHandler();
// test_harness()->Run(builder.BuildSpec());
//
// // Wait for the session shell to be intercepted.
// RunLoopUntil([&] { return fake_session_shell.is_running(); });
class FakeSessionShell : public modular::testing::FakeComponent {
 public:
  fuchsia::modular::StoryProvider* story_provider() {
    return story_provider_.get();
  }

  fuchsia::modular::SessionShellContext* session_shell_context() {
    return session_shell_context_.get();
  }

  // See modular::testing::SessionShellImpl implementation.
  void set_on_attach_view(
      fit::function<void(fuchsia::modular::ViewIdentifier view_id)> callback) {
    session_shell_impl_.set_on_attach_view(std::move(callback));
  }

  // See modular::testing::SessionShellImpl implementation.
  void set_on_detach_view(
      fit::function<void(fuchsia::modular::ViewIdentifier view_id)> callback) {
    session_shell_impl_.set_on_detach_view(std::move(callback));
  }

  // See modular::testing::SessionShellImpl implementation.
  void set_detach_delay(zx::duration detach_delay) {
    session_shell_impl_.set_detach_delay(std::move(detach_delay));
  }

 private:
  // |modular::testing::FakeComponent|
  void OnCreate(fuchsia::sys::StartupInfo startup_info) override;

  modular::testing::SessionShellImpl session_shell_impl_;
  fuchsia::modular::SessionShellContextPtr session_shell_context_;
  fuchsia::modular::StoryProviderPtr story_provider_;
};

// Simple StoryProviderWatcher that will run a user-provided lambda function
// when it sees a change in story state. Meant to be used to help monitor the
// state of the FakeSessionShell provided above.
//
// EXAMPLE USAGE (after above code snippet that initializes FakeSessionShell):
//
// // Add a function that does something when a story state change is observed.
// const char[] kStoryId = "my_story";
// modular::testing::SimpleStoryProviderWatcher watcher;
// watcher.set_on_change([kStoryId](StoryInfo story_info,
//                                  StoryState story_state,
//                                  StoryVisibilityState _) {
//   EXPECT_EQ(story_info.id, kStoryId);
// });
// watcher.Watch(fake_session_shell.story_provider(),
//                /*on_get_stories=*/nullptr);
class SimpleStoryProviderWatcher
    : public fuchsia::modular::StoryProviderWatcher {
 public:
  SimpleStoryProviderWatcher() : binding_(this) {}
  ~SimpleStoryProviderWatcher() override = default;

  using OnChangeFunction = fit::function<void(
      fuchsia::modular::StoryInfo, fuchsia::modular::StoryState,
      fuchsia::modular::StoryVisibilityState)>;

  void set_on_change(OnChangeFunction on_change) {
    on_change_ = std::move(on_change);
  }

  // Start watching for story state changes in the given story_provider. Takes
  // a lambda that allows the caller to do something with the StoryInfo data
  // returned by the initial call to GetStories() (this will be the state of
  // any existing stories when watching starts).
  void Watch(
      fuchsia::modular::StoryProvider* story_provider,
      fit::function<void(std::vector<fuchsia::modular::StoryInfo> stories)>*
          on_get_stories) {
    story_provider->GetStories(
        binding_.NewBinding(),
        on_get_stories != nullptr
            ? std::move(*on_get_stories)
            : [](std::vector<fuchsia::modular::StoryInfo>) {});
  }

 private:
  // |fuchsia::modular::StoryProviderWatcher|
  void OnChange(
      fuchsia::modular::StoryInfo story_info,
      fuchsia::modular::StoryState story_state,
      fuchsia::modular::StoryVisibilityState story_visibility_state) override {
    on_change_(std::move(story_info), story_state, story_visibility_state);
  }

  // |fuchsia::modular::StoryProviderWatcher|
  void OnDelete(std::string story_id) override {}

  // Optional user-provided lambda that will run with each OnChange(). Defaults
  // to doing nothing.
  OnChangeFunction on_change_ =
      [](fuchsia::modular::StoryInfo story_info,
         fuchsia::modular::StoryState story_state,
         fuchsia::modular::StoryVisibilityState story_visibility_state) {};
  fidl::Binding<fuchsia::modular::StoryProviderWatcher> binding_;
};

}  // namespace testing
}  // namespace modular

#endif  // LIB_MODULAR_TEST_HARNESS_CPP_FAKE_SESSION_SHELL_H_
