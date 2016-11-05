// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/agents/ideas.h"

#include <rapidjson/document.h>

#include "apps/maxwell/services/context_engine.fidl.h"
#include "apps/maxwell/services/proposal_manager.fidl.h"
#include "apps/modular/lib/app/application_context.h"
#include "lib/mtl/tasks/message_loop.h"

using maxwell::agents::IdeasAgent;

constexpr char IdeasAgent::kIdeaId[];

namespace {

using namespace maxwell::context_engine;
using namespace maxwell::suggestion_engine;

class IdeasAgentImpl : public IdeasAgent, public ContextSubscriberLink {
 public:
  IdeasAgentImpl()
      : app_ctx_(modular::ApplicationContext::CreateFromStartupInfo()),
        cx_(app_ctx_->ConnectToEnvironmentService<SuggestionAgentClient>()),
        in_(this),
        out_(app_ctx_->ConnectToEnvironmentService<ProposalManager>()) {
    fidl::InterfaceHandle<ContextSubscriberLink> in_handle;
    in_.Bind(GetProxy(&in_handle));
    cx_->Subscribe("/location/region", "json:string", std::move(in_handle));
  }

  void OnUpdate(ContextUpdatePtr update) override {
    FTL_LOG(INFO) << "OnUpdate from " << update->source << ": "
                  << update->json_value;

    rapidjson::Document d;
    d.Parse(update->json_value.data());

    if (d.IsString()) {
      const std::string region = d.GetString();

      std::string idea;

      if (region == "Antarctica")
        idea = "Find penguins near me";
      else if (region == "The Arctic")
        idea = "Buy a parka";
      else if (region == "America")
        idea = "Go on a road trip";

      if (idea.empty()) {
        out_->Remove(kIdeaId);
      } else {
        ProposalPtr p = Proposal::New();
        p->id = kIdeaId;
        p->on_selected = fidl::Array<ActionPtr>::New(0);
        SuggestionDisplayPropertiesPtr d = SuggestionDisplayProperties::New();

        d->icon = "";
        d->headline = idea;
        d->subheadline = "";
        d->details = "";

        p->display = std::move(d);

        out_->Propose(std::move(p));
      }
    }
  }

 private:
  std::unique_ptr<modular::ApplicationContext> app_ctx_;

  SuggestionAgentClientPtr cx_;
  fidl::Binding<ContextSubscriberLink> in_;
  ProposalManagerPtr out_;
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  IdeasAgentImpl app;
  loop.Run();
  return 0;
}
