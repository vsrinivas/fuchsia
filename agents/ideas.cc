// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/agents/ideas.h"

#include <mojo/system/main.h>
#include <rapidjson/document.h>

#include "apps/maxwell/interfaces/context_engine.mojom.h"
#include "apps/maxwell/interfaces/proposal_manager.mojom.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/run_application.h"

#include "apps/maxwell/debug.h"

using maxwell::agents::IdeasAgent;

constexpr char IdeasAgent::kIdeaId[];

namespace {

using mojo::ApplicationImplBase;
using mojo::Binding;
using mojo::InterfaceHandle;

using namespace maxwell::context_engine;
using namespace maxwell::suggestion_engine;
using namespace rapidjson;

class IdeasAgentImpl : public IdeasAgent,
                       public maxwell::DebuggableApp,
                       public ContextSubscriberLink {
 public:
  IdeasAgentImpl() : in_(this) {}

  void OnInitialize() override {
    ConnectToService(shell(), "mojo:context_engine", GetProxy(&cx_));

    ContextSubscriberLinkPtr in_ptr;
    in_.Bind(GetProxy(&in_ptr));
    cx_->Subscribe("/location/region", "json:string",
                   in_ptr.PassInterfaceHandle());

    ConnectToService(shell(), "mojo:suggestion_engine", GetProxy(&out_));
  }

  void OnUpdate(ContextUpdatePtr update) override {
    MOJO_LOG(INFO) << "OnUpdate from " << update->source << ": "
                   << update->json_value;

    Document d;
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
        p->on_selected = mojo::Array<ActionPtr>::New(0);
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
  SuggestionAgentClientPtr cx_;
  Binding<ContextSubscriberLink> in_;
  ProposalManagerPtr out_;
};

}  // namespace

MojoResult MojoMain(MojoHandle request) {
  IdeasAgentImpl app;
  return mojo::RunApplication(request, &app);
}
