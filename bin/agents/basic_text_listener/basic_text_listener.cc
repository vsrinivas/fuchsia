// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "application/lib/app/application_context.h"
#include "apps/maxwell/services/context/context_provider.fidl.h"
#include "apps/maxwell/services/suggestion/proposal_publisher.fidl.h"
#include "lib/mtl/tasks/message_loop.h"

namespace maxwell {

constexpr char kWebViewUrl[] = "file:///system/apps/web_view";

ProposalPtr MkUrlProposal(const std::string& url) {
  const std::string label = "Launch url: " + url;
  auto p = Proposal::New();
  p->id = "launch web_view";
  auto create_story = CreateStory::New();
  create_story->module_id = kWebViewUrl;

  // TODO(travismart,thatguy): How to load a specifc URL in web_view?
  //create_story->initial_data = "{\"view\": {\"uri\": \"http://www.yahoo.com\" } }";

  auto action = Action::New();
  action->set_create_story(std::move(create_story));
  p->on_selected.push_back(std::move(action));

  auto d = SuggestionDisplay::New();
  d->headline = label;
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
        proposal_out_(app_context_->ConnectToEnvironmentService<ProposalPublisher>()),
        binding_(this) {
    FTL_LOG(INFO) << "Initializing";
    auto query = ContextQuery::New();
    query->topics.push_back("raw/text");
    provider_->Subscribe(std::move(query), binding_.NewBinding());
  }

 private:
  // |ContextListener|
  void OnUpdate(ContextUpdatePtr result) override {
    const auto& values = result.get()->values;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
      const std::string key = it.GetKey();
      const std::string value = it.GetValue();
      FTL_LOG(INFO) << key << " : " << value;
      proposal_out_->Propose(MkUrlProposal(value));
    }
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
