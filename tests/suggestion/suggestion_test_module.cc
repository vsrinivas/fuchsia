// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/app_driver/cpp/module_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/callback/scoped_callback.h>
#include <lib/component/cpp/connect.h>

#include "peridot/public/lib/integration_testing/cpp/reporting.h"
#include "peridot/public/lib/integration_testing/cpp/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/suggestion/defs.h"

using modular::testing::Await;
using modular::testing::Signal;
using modular::testing::TestPoint;

namespace {

// Cf. README.md for what this test does and how.
class TestModule : fuchsia::modular::ProposalListener {
 public:
  TestPoint initialized_{"Root module initialized"};
  TestPoint received_story_id_{"Root module received story id"};
  TestPoint proposal_was_accepted_{"fuchsia::modular::Proposal was accepted"};

  TestModule(modular::ModuleHost* module_host,
             fidl::InterfaceRequest<
                 fuchsia::ui::app::ViewProvider> /*view_provider_request*/)
      : module_host_(module_host) {
    modular::testing::Init(module_host_->startup_context(), __FILE__);
    initialized_.Pass();

    fuchsia::modular::IntelligenceServicesPtr intelligence_services;

    module_host_->startup_context()->ConnectToEnvironmentService(
        intelligence_services.NewRequest());
    intelligence_services->GetProposalPublisher(
        proposal_publisher_.NewRequest());

    module_host_->module_context()->GetStoryId(
        [this](const fidl::StringPtr& story_id) {
          received_story_id_.Pass();

          fuchsia::modular::SetFocusState focus_story;
          focus_story.focused = true;

          fuchsia::modular::StoryCommand command;
          command.set_set_focus_state(std::move(focus_story));

          // Craft a minimal suggestion proposal.
          fuchsia::modular::SuggestionDisplay suggestion_display;
          suggestion_display.headline = "foo";
          suggestion_display.subheadline = "bar";
          suggestion_display.details = "baz";
          suggestion_display.color = 0xffff0000;

          fuchsia::modular::Proposal proposal;
          proposal.id = kProposalId;
          proposal.affinity.resize(0);
          proposal.story_name = story_id;
          proposal.display = std::move(suggestion_display);
          proposal.on_selected.push_back(std::move(command));
          proposal_listener_bindings_.AddBinding(
              this, proposal.listener.NewRequest());

          proposal_publisher_->Propose(std::move(proposal));

          Await("suggestion_proposal_received", [this] {
            Await("proposal_was_accepted", [this] {
              proposal_was_accepted_.Pass();
              Signal(kSuggestionTestModuleDone);
            });
          });
        });
  }

  TestModule(modular::ModuleHost* const module_host,
             fidl::InterfaceRequest<
                 fuchsia::ui::viewsv1::ViewProvider> /*view_provider_request*/)
      : TestModule(
            module_host,
            fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider>(nullptr)) {}

  // Called by ModuleDriver.
  TestPoint stopped_{"Root module stopped"};
  void Terminate(const std::function<void()>& done) {
    stopped_.Pass();
    modular::testing::Done(done);
  }

  // |fuchsia::modular::ProposalListener|
  void OnProposalAccepted(fidl::StringPtr proposal_id,
                          fidl::StringPtr story_id) override {
    Signal("proposal_was_accepted");
  }

 private:
  modular::ModuleHost* const module_host_;
  fuchsia::modular::ModuleContextPtr module_context_;
  fuchsia::modular::ProposalPublisherPtr proposal_publisher_;
  fidl::BindingSet<fuchsia::modular::ProposalListener>
      proposal_listener_bindings_;
  FXL_DISALLOW_COPY_AND_ASSIGN(TestModule);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = component::StartupContext::CreateFromStartupInfo();
  modular::ModuleDriver<TestModule> driver(context.get(),
                                           [&loop] { loop.Quit(); });
  loop.Run();
  return 0;
}
