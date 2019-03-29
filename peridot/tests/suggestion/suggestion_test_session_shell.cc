// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/component/cpp/connect.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/macros.h>

#include "peridot/lib/testing/component_main.h"
#include "peridot/lib/testing/session_shell_base.h"
#include "peridot/public/lib/integration_testing/cpp/reporting.h"
#include "peridot/public/lib/integration_testing/cpp/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/suggestion/defs.h"

using modular::testing::Await;
using modular::testing::Signal;
using modular::testing::TestPoint;

namespace {

const char kStoryName[] = "story";

// Cf. README.md for what this test does and how.
class TestApp : fuchsia::modular::NextListener,
                public modular::testing::SessionShellBase {
 public:
  TestApp(component::StartupContext* const startup_context)
      : SessionShellBase(startup_context) {
    TestInit(__FILE__);

    startup_context->ConnectToEnvironmentService(puppet_master_.NewRequest());

    session_shell_context()->GetSuggestionProvider(
        suggestion_provider_.NewRequest());

    suggestion_provider_->SubscribeToNext(
        suggestion_listener_bindings_.AddBinding(this),
        20 /* arbitrarily chosen */);

    CreateStory();
  }

  ~TestApp() override = default;

 private:
  void CreateStory() {
    std::vector<fuchsia::modular::StoryCommand> commands;
    fuchsia::modular::AddMod add_mod;
    add_mod.mod_name_transitional = "root";
    add_mod.intent.action = kSuggestionTestAction;
    add_mod.intent.handler = kSuggestionTestModule;

    fuchsia::modular::StoryCommand command;
    command.set_add_mod(std::move(add_mod));
    commands.push_back(std::move(command));

    puppet_master_->ControlStory(kStoryName, story_puppet_master_.NewRequest());
    story_puppet_master_->Enqueue(std::move(commands));
    story_puppet_master_->Execute(
        [this](fuchsia::modular::ExecuteResult result) { StartStory(); });

    Await(kSuggestionTestModuleDone, [this] {
      story_controller_->Stop([this] {
        story_controller_.Unbind();
        Signal(modular::testing::kTestShutdown);
      });
    });
  }

  void StartStory() {
    story_provider()->GetController(kStoryName, story_controller_.NewRequest());
    story_controller_.set_error_handler([](zx_status_t status) {
      FXL_LOG(ERROR) << "Story controller for story " << kStoryName
                     << " died. Does this story exist?";
    });
    story_controller_->RequestStart();
  }

  TestPoint received_suggestion_{
      "SuggestionTestSessionShell received suggestion"};

  // |fuchsia::modular::NextListener|
  void OnNextResults(
      std::vector<fuchsia::modular::Suggestion> suggestions) override {
    for (auto& suggestion : suggestions) {
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

  fuchsia::modular::PuppetMasterPtr puppet_master_;
  fuchsia::modular::StoryPuppetMasterPtr story_puppet_master_;
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
