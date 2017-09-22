// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/app/cpp/connect.h"
#include "lib/suggestion/fidl/proposal.fidl.h"
#include "lib/suggestion/fidl/proposal_publisher.fidl.h"
#include "peridot/lib/testing/component_base.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "lib/module/fidl/module.fidl.h"
#include "lib/fsl/tasks/message_loop.h"

using modular::testing::TestPoint;

namespace {

// This is how long we wait for the test to finish before we timeout and tear
// down our test.
constexpr int kTimeoutMilliseconds = 5000;
constexpr char kProposalId[] =
    "file:///system/apps/moudlar_tests/suggestion_proposal_test#proposal";

class SuggestionApp : modular::testing::ComponentBase<modular::Module> {
 public:
  static void New() {
    new SuggestionApp;  // deleted in Stop();
  }

 private:
  SuggestionApp() { TestInit(__FILE__); }
  ~SuggestionApp() override = default;

  // |Module|
  void Initialize(
      fidl::InterfaceHandle<modular::ModuleContext> module_context,
      fidl::InterfaceHandle<app::ServiceProvider> /*incoming_services*/,
      fidl::InterfaceRequest<app::ServiceProvider> /*outgoing_services*/)
      override {
    module_context_.Bind(std::move(module_context));
    initialized_.Pass();

    maxwell::IntelligenceServicesPtr intelligence_services;
    module_context_->GetIntelligenceServices(
        intelligence_services.NewRequest());
    intelligence_services->GetProposalPublisher(
        proposal_publisher_.NewRequest());

    module_context_->GetStoryId([this](const fidl::String& story_id) {
      received_story_id_.Pass();

      auto focus_story = maxwell::FocusStory::New();
      focus_story->story_id = story_id;

      auto action = maxwell::Action::New();
      action->set_focus_story(std::move(focus_story));

      // Craft a minimal suggestion proposal.
      auto suggestion_display = maxwell::SuggestionDisplay::New();
      suggestion_display->headline = "foo";
      suggestion_display->subheadline = "bar";
      suggestion_display->details = "baz";
      suggestion_display->color = 0xffff0000;
      suggestion_display->icon_urls.resize(0);
      suggestion_display->image_url = "";
      suggestion_display->image_type = maxwell::SuggestionImageType::OTHER;

      auto proposal = maxwell::Proposal::New();
      proposal->id = kProposalId;
      proposal->display = std::move(suggestion_display);
      proposal->on_selected.push_back(std::move(action));

      proposal_publisher_->Propose(std::move(proposal));

      modular::testing::GetStore()->Get(
          "suggestion_proposal_received",
          [this](const fidl::String&) { module_context_->Done(); });
    });

    // Start a timer to quit in case another test component misbehaves and we
    // time out.
    fsl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
        Protect([this] { DeleteAndQuit([] {}); }),
        fxl::TimeDelta::FromMilliseconds(kTimeoutMilliseconds));
  }

  // |Lifecycle|
  void Terminate() override {
    stopped_.Pass();
    DeleteAndQuitAndUnbind();
  }

  modular::ModuleContextPtr module_context_;
  maxwell::ProposalPublisherPtr proposal_publisher_;

  TestPoint initialized_{"Root module initialized"};
  TestPoint received_story_id_{"Root module received story id"};
  TestPoint stopped_{"Root module stopped"};
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  SuggestionApp::New();
  loop.Run();
  return 0;
}
