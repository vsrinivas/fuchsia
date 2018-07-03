// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/app/cpp/connect.h>
#include <lib/app/cpp/startup_context.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/macros.h>

#include "peridot/lib/testing/component_base.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/suggestion/defs.h"

using modular::testing::Await;
using modular::testing::Signal;
using modular::testing::TestPoint;

namespace {

// Cf. README.md for what this test does and how.
class TestApp
    : fuchsia::modular::NextListener,
      public modular::testing::ComponentBase<fuchsia::modular::UserShell> {
 public:
  TestApp(fuchsia::sys::StartupContext* const startup_context)
      : ComponentBase(startup_context) {
    TestInit(__FILE__);
  }

  ~TestApp() override = default;

 private:
  TestPoint initialized_{"SuggestionTestUserShell initialized"};

  // |fuchsia::modular::UserShell|
  void Initialize(fidl::InterfaceHandle<fuchsia::modular::UserShellContext>
                      user_shell_context) override {
    user_shell_context_.Bind(std::move(user_shell_context));

    user_shell_context_->GetStoryProvider(story_provider_.NewRequest());
    user_shell_context_->GetSuggestionProvider(
        suggestion_provider_.NewRequest());

    suggestion_provider_->SubscribeToNext(
        suggestion_listener_bindings_.AddBinding(this),
        20 /* arbitrarily chosen */);

    story_provider_->CreateStory(
        kSuggestionTestModule,
        [this](const fidl::StringPtr& story_id) { StartStoryById(story_id); });

    initialized_.Pass();

    Await(kSuggestionTestModuleDone, [this] {
      story_controller_->Stop([this] {
        story_controller_.Unbind();
        Signal(modular::testing::kTestShutdown);
      });
    });
  }

  void StartStoryById(const fidl::StringPtr& story_id) {
    story_provider_->GetController(story_id, story_controller_.NewRequest());
    story_controller_.set_error_handler([this, story_id] {
      FXL_LOG(ERROR) << "Story controller for story " << story_id
                     << " died. Does this story exist?";
    });

    story_controller_->Start(view_owner_.NewRequest());
  }

  TestPoint received_suggestion_{"SuggestionTestUserShell received suggestion"};

  // |fuchsia::modular::NextListener|
  void OnNextResults(
      fidl::VectorPtr<fuchsia::modular::Suggestion> suggestions) override {
    for (auto& suggestion : *suggestions) {
      auto& display = suggestion.display;
      if (display.headline == "foo" && display.subheadline == "bar" &&
          display.details == "baz") {
        modular::testing::GetStore()->Put("suggestion_proposal_received", "",
                                          [] {});
        received_suggestion_.Pass();
        fuchsia::modular::Interaction interaction;
        interaction.type = fuchsia::modular::InteractionType::SELECTED;
        suggestion_provider_->NotifyInteraction(suggestion.uuid, interaction);
        break;
      }
    }
  }

  // |fuchsia::modular::NextListener|
  void OnProcessingChange(bool processing) override {}

  fuchsia::ui::views_v1_token::ViewOwnerPtr view_owner_;
  fuchsia::modular::UserShellContextPtr user_shell_context_;
  fuchsia::modular::StoryProviderPtr story_provider_;
  fuchsia::modular::StoryControllerPtr story_controller_;
  fuchsia::modular::SuggestionProviderPtr suggestion_provider_;
  fidl::BindingSet<fuchsia::modular::NextListener>
      suggestion_listener_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  modular::testing::ComponentMain<TestApp>();
  return 0;
}
