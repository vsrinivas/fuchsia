// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/cpp/modular.h>
#include "lib/app/cpp/application_context.h"
#include "lib/context/cpp/context_helper.h"
#include "lib/fsl/tasks/message_loop.h"
#include "peridot/bin/agents/entity_utils/entity_span.h"
#include "peridot/bin/agents/entity_utils/entity_utils.h"
#include "third_party/rapidjson/rapidjson/document.h"

namespace modular {

constexpr char kWebViewUrl[] = "web_view";
// TODO(travismart): This url breaks in web_view because it's running an
// "unsupported browser." Follow up on this.
const std::string kGmailUrlPrefix =
    "https://mail.google.com/mail/?view=cm&fs=1&tf=1&to=";

Proposal MkUrlProposal(const std::string& query) {
  Proposal p;
  p.id = "launch web_view";
  CreateStory create_story;
  create_story.module_id = kWebViewUrl;

  create_story.initial_data =
      "{\"view\": {\"uri\": \"" + kGmailUrlPrefix + query + "\" } }";

  Action action;
  action.set_create_story(std::move(create_story));
  p.on_selected.push_back(std::move(action));

  SuggestionDisplay d;
  d.headline = "Compose email to: " + query;
  d.color = 0xff4285f4;

  p.display = std::move(d);
  return p;
}

// Subscribe to selected entities in ApplicationContext, and Suggest any found
// selected entities to the user.
class ProposalMaker : ContextListener {
 public:
  ProposalMaker()
      : app_context_(component::ApplicationContext::CreateFromStartupInfo()),
        reader_(app_context_->ConnectToEnvironmentService<ContextReader>()),
        proposal_out_(
            app_context_->ConnectToEnvironmentService<ProposalPublisher>()),
        binding_(this) {
    ContextQuery query;
    ContextSelector selector;
    selector.type = ContextValueType::ENTITY;
    selector.meta = ContextMetadata::New();
    selector.meta->entity = EntityMetadata::New();
    selector.meta->entity->topic = kSelectedEntitiesTopic;
    AddToContextQuery(&query, kSelectedEntitiesTopic, std::move(selector));
    reader_->Subscribe(std::move(query), binding_.NewBinding());
  }

 private:
  // |ContextListener|
  void OnContextUpdate(ContextUpdate result) override {
    auto p = TakeContextValue(&result, kSelectedEntitiesTopic);
    if (!p.first || p.second->empty())
      return;
    const std::vector<EntitySpan> entities =
        EntitySpan::FromContextValues(p.second);
    for (const EntitySpan& e : entities) {
      if (e.GetType() == kEmailType) {
        proposal_out_->Propose(MkUrlProposal(e.GetContent()));
      }
      // TODO(travismart): Propose more deep links based on entity type.
      else {
        FXL_LOG(ERROR) << "SelectedEntity type not recognized: " << e.GetType();
      }
    }
    // TODO(travismart): UnPropose an unselected entity.
  }

  std::unique_ptr<component::ApplicationContext> app_context_;
  ContextReaderPtr reader_;
  ProposalPublisherPtr proposal_out_;
  fidl::Binding<ContextListener> binding_;
};

}  // namespace modular

int main(int argc, const char** argv) {
  fsl::MessageLoop loop;
  modular::ProposalMaker app;
  loop.Run();
  return 0;
}
