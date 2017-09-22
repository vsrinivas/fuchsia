// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/app/cpp/connect.h"
#include "lib/app/fidl/service_provider.fidl.h"
#include "lib/suggestion/fidl/suggestion_provider.fidl.h"
#include "peridot/lib/testing/component_base.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "lib/user/fidl/user_shell.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fsl/tasks/message_loop.h"

using modular::testing::TestPoint;

namespace {

class SuggestionTestUserShellApp
    : modular::StoryWatcher,
      maxwell::SuggestionListener,
      modular::testing::ComponentBase<modular::UserShell> {
 public:
  static void New() {
    new SuggestionTestUserShellApp;  // Deleted in Terminate().
  }

 private:
  SuggestionTestUserShellApp() : story_watcher_binding_(this) {
    TestInit(__FILE__);
  }
  ~SuggestionTestUserShellApp() override = default;

  // |UserShell|
  void Initialize(fidl::InterfaceHandle<modular::UserShellContext>
                      user_shell_context) override {
    user_shell_context_.Bind(std::move(user_shell_context));

    user_shell_context_->GetStoryProvider(story_provider_.NewRequest());
    user_shell_context_->GetSuggestionProvider(
        suggestion_provider_.NewRequest());

    suggestion_provider_->SubscribeToNext(
        suggestion_listener_bindings_.AddBinding(this),
        next_controller_.NewRequest());
    next_controller_->SetResultCount(20 /* arbitrarily chosen */);

    story_provider_->CreateStory(
        "file:///system/apps/modular_tests/suggestion_proposal_test_module",
        [this](const fidl::String& story_id) { StartStoryById(story_id); });
    initialized_.Pass();
  }

  // |UserShell|
  void Terminate() override {
    TEST_PASS("Suggestion test user shell terminated");
    DeleteAndQuit();
  };

  void StartStoryById(const fidl::String& story_id) {
    story_provider_->GetController(story_id, story_controller_.NewRequest());
    story_controller_.set_connection_error_handler([this, story_id] {
      FXL_LOG(ERROR) << "Story controller for story " << story_id
                     << " died. Does this story exist?";
    });

    story_controller_->Watch(story_watcher_binding_.NewBinding());

    story_controller_->Start(view_owner_.NewRequest());
  }

  // |StoryWatcher|
  void OnStateChange(modular::StoryState state) override {
    if (state != modular::StoryState::DONE) {
      return;
    }
    story_controller_->Stop([this] {
      story_watcher_binding_.Close();
      story_controller_.reset();

      user_shell_context_->Logout();
    });
  }

  // |StoryWatcher|
  void OnModuleAdded(modular::ModuleDataPtr /*module_data*/) override {}

  // |SuggestionListener|
  void OnAdd(fidl::Array<maxwell::SuggestionPtr> suggestions) override {
    for (auto& suggestion : suggestions) {
      auto& display = suggestion->display;
      if (display->headline == "foo" && display->subheadline == "bar" &&
          display->details == "baz") {
        modular::testing::GetStore()->Put("suggestion_proposal_received", "",
                                          [] {});
        received_suggestion_.Pass();
        break;
      }
    }
  }

  // |SuggestionListener|
  void OnRemove(const fidl::String& /*suggestion_id*/) override {}

  // |SuggestionListener|
  void OnRemoveAll() override {}

  mozart::ViewOwnerPtr view_owner_;

  modular::UserShellContextPtr user_shell_context_;
  modular::StoryProviderPtr story_provider_;
  modular::StoryControllerPtr story_controller_;
  fidl::Binding<modular::StoryWatcher> story_watcher_binding_;

  maxwell::SuggestionProviderPtr suggestion_provider_;
  maxwell::NextControllerPtr next_controller_;
  fidl::BindingSet<maxwell::SuggestionListener> suggestion_listener_bindings_;

  TestPoint initialized_{"SuggestionTestUserShell initialized"};
  TestPoint received_suggestion_{"SuggestionTestUserShell received suggestion"};

  FXL_DISALLOW_COPY_AND_ASSIGN(SuggestionTestUserShellApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  SuggestionTestUserShellApp::New();
  loop.Run();
  return 0;
}
