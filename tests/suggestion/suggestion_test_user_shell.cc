// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/app/connect.h"
#include "application/services/service_provider.fidl.h"
#include "apps/maxwell/services/suggestion/suggestion_provider.fidl.h"
#include "apps/modular/lib/fidl/single_service_app.h"
#include "apps/modular/lib/testing/reporting.h"
#include "apps/modular/lib/testing/testing.h"
#include "apps/modular/services/user/user_context.fidl.h"
#include "apps/modular/services/user/user_shell.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

using modular::testing::TestPoint;

namespace {

class SuggestionTestUserShellApp
    : public modular::StoryWatcher,
      public maxwell::SuggestionListener,
      public modular::SingleServiceApp<modular::UserShell> {
 public:
  SuggestionTestUserShellApp() : story_watcher_binding_(this) {
    modular::testing::Init(application_context(), __FILE__);
  }
  ~SuggestionTestUserShellApp() override = default;

 private:
  // |UserShell|
  void Initialize(fidl::InterfaceHandle<modular::UserContext> user_context,
                  fidl::InterfaceHandle<modular::UserShellContext>
                      user_shell_context) override {
    user_context_.Bind(std::move(user_context));

    auto user_shell_context_ptr =
        modular::UserShellContextPtr::Create(std::move(user_shell_context));
    user_shell_context_ptr->GetStoryProvider(story_provider_.NewRequest());
    user_shell_context_ptr->GetSuggestionProvider(
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
  void Terminate(const TerminateCallback& done) override {
    mtl::MessageLoop::GetCurrent()->PostQuitTask();
    TEST_PASS("Suggestion test user shell terminated");
    modular::testing::Teardown();
    done();
  };

  void StartStoryById(const fidl::String& story_id) {
    story_provider_->GetController(story_id, story_controller_.NewRequest());
    story_controller_.set_connection_error_handler([this, story_id] {
      FTL_LOG(ERROR) << "Story controller for story " << story_id
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

      user_context_->Logout();
    });
  }

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
  void OnRemove(const fidl::String& suggestion_id) override {}

  // |SuggestionListener|
  void OnRemoveAll() override {}

  mozart::ViewOwnerPtr view_owner_;

  modular::UserContextPtr user_context_;
  modular::StoryProviderPtr story_provider_;
  modular::StoryControllerPtr story_controller_;
  fidl::Binding<modular::StoryWatcher> story_watcher_binding_;

  maxwell::SuggestionProviderPtr suggestion_provider_;
  maxwell::NextControllerPtr next_controller_;
  fidl::BindingSet<maxwell::SuggestionListener> suggestion_listener_bindings_;

  TestPoint initialized_{"SuggestionTestUserShell initialized"};
  TestPoint received_suggestion_{"SuggestionTestUserShell received suggestion"};

  FTL_DISALLOW_COPY_AND_ASSIGN(SuggestionTestUserShellApp);
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  SuggestionTestUserShellApp app;
  loop.Run();
  return 0;
}
