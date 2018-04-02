// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/cpp/modular.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "garnet/lib/callback/scoped_callback.h"
#include "lib/app/cpp/connect.h"
#include "lib/app_driver/cpp/module_driver.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"

using modular::testing::TestPoint;

namespace {

// This is how long we wait for the test to finish before we timeout and tear
// down our test.
constexpr int kTimeoutMilliseconds = 5000;
constexpr char kProposalId[] =
    "file:///system/bin/modular_tests/suggestion_proposal_test#proposal";

class SuggestionApp {
 public:
  SuggestionApp(
      modular::ModuleHost* module_host,
      fidl::InterfaceRequest<views_v1::ViewProvider> /*view_provider_request*/,
      fidl::InterfaceRequest<component::ServiceProvider> /*outgoing_services*/)
      : module_host_(module_host), weak_ptr_factory_(this) {
    modular::testing::Init(module_host_->application_context(), __FILE__);
    initialized_.Pass();

    modular::IntelligenceServicesPtr intelligence_services;
    module_host_->module_context()->GetIntelligenceServices(
        intelligence_services.NewRequest());
    intelligence_services->GetProposalPublisher(
        proposal_publisher_.NewRequest());

    module_host_->module_context()->GetStoryId(
        [this](const fidl::StringPtr& story_id) {
          received_story_id_.Pass();

          modular::FocusStory focus_story;
          focus_story.story_id = story_id;

          modular::Action action;
          action.set_focus_story(std::move(focus_story));

          // Craft a minimal suggestion proposal.
          modular::SuggestionDisplay suggestion_display;
          suggestion_display.headline = "foo";
          suggestion_display.subheadline = "bar";
          suggestion_display.details = "baz";
          suggestion_display.color = 0xffff0000;

          modular::Proposal proposal;
          proposal.id = kProposalId;
          proposal.display = std::move(suggestion_display);
          proposal.on_selected.push_back(std::move(action));

          proposal_publisher_->Propose(std::move(proposal));

          modular::testing::GetStore()->Get(
              "suggestion_proposal_received", [this](const fidl::StringPtr&) {
                module_host_->module_context()->Done();
              });
        });

    // Start a timer to quit in case another test component misbehaves and we
    // time out.
    async::PostDelayedTask(
        async_get_default(),
        callback::MakeScoped(
            weak_ptr_factory_.GetWeakPtr(),
            [this] { module_host_->module_context()->Done(); }),
        zx::msec(kTimeoutMilliseconds));
  }

  // Called by ModuleDriver.
  void Terminate(const std::function<void()>& done) {
    stopped_.Pass();
    modular::testing::Done(done);
  }

 private:
  modular::ModuleHost* const module_host_;
  modular::ModuleContextPtr module_context_;
  modular::ProposalPublisherPtr proposal_publisher_;

  TestPoint initialized_{"Root module initialized"};
  TestPoint received_story_id_{"Root module received story id"};
  TestPoint stopped_{"Root module stopped"};

  fxl::WeakPtrFactory<SuggestionApp> weak_ptr_factory_;
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  auto app_context = component::ApplicationContext::CreateFromStartupInfo();
  modular::ModuleDriver<SuggestionApp> driver(app_context.get(),
                                              [&loop] { loop.QuitNow(); });
  loop.Run();
  return 0;
}
