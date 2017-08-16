// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/agents/ideas.h"

#include <rapidjson/document.h>

#include "application/lib/app/application_context.h"
#include "apps/maxwell/services/context/context_reader.fidl.h"
#include "apps/maxwell/services/suggestion/proposal_publisher.fidl.h"
#include "lib/mtl/tasks/message_loop.h"

constexpr char maxwell::agents::IdeasAgent::kIdeaId[];

namespace maxwell {
namespace {

const char kLocationTopic[] = "location/region";

class IdeasAgentApp : public agents::IdeasAgent, public ContextListener {
 public:
  IdeasAgentApp()
      : app_context_(app::ApplicationContext::CreateFromStartupInfo()),
        reader_(app_context_->ConnectToEnvironmentService<ContextReader>()),
        binding_(this),
        out_(app_context_->ConnectToEnvironmentService<ProposalPublisher>()) {
    auto query = ContextQuery::New();
    auto selector = ContextSelector::New();
    selector->type = ContextValueType::ENTITY;
    selector->meta = ContextMetadata::New();
    selector->meta->entity = EntityMetadata::New();
    selector->meta->entity->topic = kLocationTopic;
    query->selector[kLocationTopic] = std::move(selector);
    reader_->Subscribe(std::move(query), binding_.NewBinding());
  }

  void OnContextUpdate(ContextUpdatePtr update) override {
    if (update->values[kLocationTopic].empty()) return;
    rapidjson::Document d;
    d.Parse(update->values[kLocationTopic][0]->content.data());

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
        auto p = Proposal::New();
        p->id = kIdeaId;
        p->on_selected = fidl::Array<ActionPtr>::New(0);
        auto d = SuggestionDisplay::New();

        d->headline = idea;
        d->subheadline = "";
        d->details = "";
        d->color = 0x00aaaa00;  // argb yellow
        d->icon_urls = fidl::Array<fidl::String>::New(1);
        d->icon_urls[0] = "";
        d->image_url = "";
        d->image_type = SuggestionImageType::PERSON;

        p->display = std::move(d);

        out_->Propose(std::move(p));
      }
    }
  }

 private:
  std::unique_ptr<app::ApplicationContext> app_context_;

  ContextReaderPtr reader_;
  fidl::Binding<ContextListener> binding_;
  ProposalPublisherPtr out_;
};

}  // namespace
}  // namespace maxwell

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  maxwell::IdeasAgentApp app;
  loop.Run();
  return 0;
}
