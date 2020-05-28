// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_MODULAR_TEST_HARNESS_CPP_FAKE_SESSION_SHELL_H_
#define SRC_MODULAR_LIB_MODULAR_TEST_HARNESS_CPP_FAKE_SESSION_SHELL_H_

#include <lib/modular/testing/cpp/fake_component.h>

#include <sdk/lib/sys/cpp/component_context.h>

#include "src/modular/lib/testing/session_shell_impl.h"

namespace modular_testing {

// Session shell fake that provides access to the StoryProvider, the
// SessionShellContext, and a test implementation of SessionShellImpl.
//
// EXAMPLE USAGE:
//
// ...
// modular_testing::TestHarnessBuilder builder;
// auto fake_session_shell = FakeSessionShell::CreateWithDefaultOptions();
//
// builder.InterceptSessionShell(fake_session_shell.BuildInterceptOptions());
// builder.BuildAndRun(test_harness());
//
// // Wait for the session shell to be intercepted.
// RunLoopUntil([&] { return fake_session_shell->is_running(); });
// ...
class FakeSessionShell : public modular_testing::FakeComponent {
 public:
  explicit FakeSessionShell(FakeComponent::Args args);

  ~FakeSessionShell() override;

  // Instantiates a FakeSessionShell with a randomly generated URL and default sandbox services
  // (see GetDefaultSandboxServices()).
  static std::unique_ptr<FakeSessionShell> CreateWithDefaultOptions();

  // Returns the default list of services (capabilities) a session shell expects in its namespace.
  // This method is useful when setting up a session shell for interception.
  //
  // Default services:
  //  * fuchsia.modular.ComponentContext
  //  * fuchsia.modular.SessionShellContext
  //  * fuchsia.modular.PuppetMaster
  static std::vector<std::string> GetDefaultSandboxServices();

  // Requires: FakeComponent::is_running()
  fuchsia::modular::StoryProvider* story_provider() { return story_provider_.get(); }

  // Requires: FakeComponent::is_running()
  fuchsia::modular::SessionShellContext* session_shell_context() {
    return session_shell_context_.get();
  }

  // See modular_testing::SessionShellImpl implementation.
  void set_on_attach_view(fit::function<void(fuchsia::modular::ViewIdentifier view_id)> callback) {
    session_shell_impl_.set_on_attach_view(std::move(callback));
  }

  // See modular_testing::SessionShellImpl implementation.
  void set_on_detach_view(fit::function<void(fuchsia::modular::ViewIdentifier view_id)> callback) {
    session_shell_impl_.set_on_detach_view(std::move(callback));
  }

  // See modular_testing::SessionShellImpl implementation.
  void set_detach_delay(zx::duration detach_delay) {
    session_shell_impl_.set_detach_delay(std::move(detach_delay));
  }

 protected:
  // |modular_testing::FakeComponent|
  void OnCreate(fuchsia::sys::StartupInfo startup_info) override;

 private:
  modular_testing::SessionShellImpl session_shell_impl_;
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
// modular_testing::SimpleStoryProviderWatcher watcher;
// watcher.set_on_change_2([kStoryId](StoryInfo2 story_info,
//                                    StoryState story_state,
//                                    StoryVisibilityState _) {
//   EXPECT_EQ(story_info.id, kStoryId);
// });
// watcher.Watch(fake_session_shell.story_provider(),
//                /*on_get_stories=*/nullptr);
class SimpleStoryProviderWatcher : public fuchsia::modular::StoryProviderWatcher {
 public:
  SimpleStoryProviderWatcher() : binding_(this) {}
  ~SimpleStoryProviderWatcher() override = default;

  using OnChange2Function =
      fit::function<void(fuchsia::modular::StoryInfo2, fuchsia::modular::StoryState,
                         fuchsia::modular::StoryVisibilityState)>;
  using OnDeleteFunction = fit::function<void(std::string)>;

  void set_on_change_2(OnChange2Function on_change_2) { on_change_2_ = std::move(on_change_2); }
  void set_on_delete(OnDeleteFunction on_delete) { on_delete_ = std::move(on_delete); }

  // Start watching for story state changes in the given story_provider. Takes
  // a lambda that allows the caller to do something with the StoryInfo data
  // returned by the initial call to GetStories() (this will be the state of
  // any existing stories when watching starts).
  void Watch(
      fuchsia::modular::StoryProvider* story_provider,
      fit::function<void(std::vector<fuchsia::modular::StoryInfo2> stories)>* on_get_stories) {
    story_provider->GetStories2(
        binding_.NewBinding(), on_get_stories != nullptr
                                   ? std::move(*on_get_stories)
                                   : [](std::vector<fuchsia::modular::StoryInfo2>) {});
  }

 private:
  // |fuchsia::modular::StoryProviderWatcher|
  void OnChange2(fuchsia::modular::StoryInfo2 story_info, fuchsia::modular::StoryState story_state,
                 fuchsia::modular::StoryVisibilityState story_visibility_state) override {
    if (!on_change_2_) {
      return;
    }
    on_change_2_(std::move(story_info), story_state, story_visibility_state);
  }

  // |fuchsia::modular::StoryProviderWatcher|
  void OnDelete(std::string story_id) override {
    if (!on_delete_) {
      return;
    }
    on_delete_(story_id);
  }

  // Optional user-provided lambda that will run with each OnChange2().
  OnChange2Function on_change_2_;
  // Optional user-provided lambda that will run with each OnDelete().
  OnDeleteFunction on_delete_;

  fidl::Binding<fuchsia::modular::StoryProviderWatcher> binding_;
};

}  // namespace modular_testing

#endif  // SRC_MODULAR_LIB_MODULAR_TEST_HARNESS_CPP_FAKE_SESSION_SHELL_H_
