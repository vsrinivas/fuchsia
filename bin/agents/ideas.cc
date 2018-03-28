// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/agents/ideas.h"

#include <rapidjson/document.h>

#include <fuchsia/cpp/modular.h>

#include "lib/app/cpp/application_context.h"
#include "lib/context/cpp/context_helper.h"
#include "lib/fsl/tasks/message_loop.h"

constexpr char maxwell::agents::IdeasAgent::kIdeaId[];

namespace maxwell {

namespace agents {

IdeasAgent::~IdeasAgent() = default;

}  // agents

namespace {

const char kLocationTopic[] = "location/region";

class IdeasAgentApp : public agents::IdeasAgent,
                      public modular::ContextListener {
 public:
  IdeasAgentApp()
      : app_context_(component::ApplicationContext::CreateFromStartupInfo()),
        reader_(app_context_->ConnectToEnvironmentService<modular::ContextReader>()),
        binding_(this),
        out_(app_context_->ConnectToEnvironmentService<modular::ProposalPublisher>()) {
    modular::ContextQuery query;
    modular::ContextSelector selector;
    selector.type = modular::ContextValueType::ENTITY;
    selector.meta = modular::ContextMetadata::New();
    selector.meta->entity = modular::EntityMetadata::New();
    selector.meta->entity->topic = kLocationTopic;
    AddToContextQuery(&query, kLocationTopic, std::move(selector));
    reader_->Subscribe(std::move(query), binding_.NewBinding());
  }

  void OnContextUpdate(modular::ContextUpdate update) override {
    auto r = TakeContextValue(&update, kLocationTopic);
    if (!r.first)
      return;
    rapidjson::Document d;
    d.Parse(r.second->at(0).content->data());

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
        modular::Proposal p;
        p.id = kIdeaId;
        p.on_selected = fidl::VectorPtr<modular::Action>::New(0);
        modular::SuggestionDisplay d;

        d.headline = idea;
        d.color = 0x00aaaa00;  // argb yellow

        p.display = std::move(d);

        out_->Propose(std::move(p));
      }
    }
  }

 private:
  std::unique_ptr<component::ApplicationContext> app_context_;

  modular::ContextReaderPtr reader_;
  fidl::Binding<modular::ContextListener> binding_;
  modular::ProposalPublisherPtr out_;
};

}  // namespace
}  // namespace maxwell

int main(int argc, const char** argv) {
  fsl::MessageLoop loop;
  maxwell::IdeasAgentApp app;
  loop.Run();
  return 0;
}
