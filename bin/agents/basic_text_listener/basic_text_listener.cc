// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "application/lib/app/application_context.h"
#include "apps/maxwell/services/context/context_provider.fidl.h"
#include "apps/maxwell/services/suggestion/proposal_publisher.fidl.h"
#include "lib/mtl/tasks/message_loop.h"
#include "third_party/rapidjson/rapidjson/document.h"

namespace maxwell {

constexpr char kWebViewUrl[] = "file:///system/apps/web_view";

ProposalPtr MkUrlProposal(const std::string& query) {
  auto p = Proposal::New();
  p->id = "launch web_view";
  auto create_story = CreateStory::New();
  create_story->module_id = kWebViewUrl;

  create_story->initial_data =
      "{\"view\": {\"uri\": \"http://www.google.com/#q=" + query + "\" } }";

  auto action = Action::New();
  action->set_create_story(std::move(create_story));
  p->on_selected.push_back(std::move(action));

  auto d = SuggestionDisplay::New();
  d->headline = "Search Google for: " + query;
  d->subheadline = "";
  d->details = "";
  d->color = 0xff4285f4;
  d->icon_urls.push_back("");
  d->image_url = "";
  d->image_type = SuggestionImageType::OTHER;

  p->display = std::move(d);
  return p;
}

class BasicTextListener : ContextListener {
 public:
  BasicTextListener()
      : app_context_(app::ApplicationContext::CreateFromStartupInfo()),
        provider_(app_context_->ConnectToEnvironmentService<ContextProvider>()),
        proposal_out_(
            app_context_->ConnectToEnvironmentService<ProposalPublisher>()),
        binding_(this) {
    FTL_LOG(INFO) << "Initializing";
    auto query = ContextQuery::New();
    query->topics.push_back("raw/text");
    provider_->Subscribe(std::move(query), binding_.NewBinding());
  }

 private:
  // |ContextListener|
  void OnUpdate(ContextUpdatePtr result) override {
    rapidjson::Document text_doc;
    text_doc.Parse(result->values["raw/text"]);
    if (!text_doc.HasMember("text") || !text_doc["text"].IsString()) {
      FTL_LOG(ERROR) << "Invalid raw/text entry in ApplicationContext.";
    }
    const std::string raw_text = text_doc["text"].GetString();
    FTL_LOG(INFO) << "raw/text:" << raw_text;
    proposal_out_->Propose(MkUrlProposal(raw_text));
  }

  std::unique_ptr<app::ApplicationContext> app_context_;
  ContextProviderPtr provider_;
  ProposalPublisherPtr proposal_out_;
  fidl::Binding<ContextListener> binding_;
};

}  // namespace maxwell

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  maxwell::BasicTextListener app;
  loop.Run();
  return 0;
}
