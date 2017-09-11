// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/app/cpp/application_context.h"
#include "apps/maxwell/services/context/context_reader.fidl.h"
#include "apps/maxwell/services/suggestion/proposal_publisher.fidl.h"
#include "apps/maxwell/src/agents/entity_utils/entity_span.h"
#include "apps/maxwell/src/agents/entity_utils/entity_utils.h"
#include "lib/mtl/tasks/message_loop.h"
#include "third_party/rapidjson/rapidjson/document.h"

namespace maxwell {

constexpr char kWebViewUrl[] = "web_view";
// TODO(travismart): This url breaks in web_view because it's running an
// "unsupported browser." Follow up on this.
const std::string kGmailUrlPrefix =
    "https://mail.google.com/mail/?view=cm&fs=1&tf=1&to=";

ProposalPtr MkUrlProposal(const std::string& query) {
  auto p = Proposal::New();
  p->id = "launch web_view";
  auto create_story = CreateStory::New();
  create_story->module_id = kWebViewUrl;

  create_story->initial_data =
      "{\"view\": {\"uri\": \"" + kGmailUrlPrefix + query + "\" } }";

  auto action = Action::New();
  action->set_create_story(std::move(create_story));
  p->on_selected.push_back(std::move(action));

  auto d = SuggestionDisplay::New();
  d->headline = "Compose email to: " + query;
  d->subheadline = "";
  d->details = "";
  d->color = 0xff4285f4;
  d->icon_urls.push_back("");
  d->image_url = "";
  d->image_type = SuggestionImageType::OTHER;

  p->display = std::move(d);
  return p;
}

// Subscribe to selected entities in ApplicationContext, and Suggest any found
// selected entities to the user.
class ProposalMaker : ContextListener {
 public:
  ProposalMaker()
      : app_context_(app::ApplicationContext::CreateFromStartupInfo()),
        reader_(app_context_->ConnectToEnvironmentService<ContextReader>()),
        proposal_out_(
            app_context_->ConnectToEnvironmentService<ProposalPublisher>()),
        binding_(this) {
    auto query = ContextQuery::New();
    auto selector = ContextSelector::New();
    selector->type = ContextValueType::ENTITY;
    selector->meta = ContextMetadata::New();
    selector->meta->entity = EntityMetadata::New();
    selector->meta->entity->topic = kSelectedEntitiesTopic;
    query->selector[kSelectedEntitiesTopic] = std::move(selector);
    reader_->Subscribe(std::move(query), binding_.NewBinding());
  }

 private:
  // |ContextListener|
  void OnContextUpdate(ContextUpdatePtr result) override {
    if (result->values[kSelectedEntitiesTopic].empty()) return;
    const std::vector<EntitySpan> entities =
        EntitySpan::FromContextValues(result->values[kSelectedEntitiesTopic]);
    for (const EntitySpan& e : entities) {
      if (e.GetType() == kEmailType) {
        proposal_out_->Propose(MkUrlProposal(e.GetContent()));
      }
      // TODO(travismart): Propose more deep links based on entity type.
      else {
        FTL_LOG(ERROR) << "SelectedEntity type not recognized: " << e.GetType();
      }
    }
    // TODO(travismart): UnPropose an unselected entity.
  }

  std::unique_ptr<app::ApplicationContext> app_context_;
  ContextReaderPtr reader_;
  ProposalPublisherPtr proposal_out_;
  fidl::Binding<ContextListener> binding_;
};

}  // namespace maxwell

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  maxwell::ProposalMaker app;
  loop.Run();
  return 0;
}
