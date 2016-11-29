// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unordered_map>

#include "apps/maxwell/services/context/client.fidl.h"
#include "apps/maxwell/services/suggestion/suggestion_agent_client.fidl.h"
#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/src/document_store/documents.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

using namespace document_store;

struct ProposalContent {
  std::string url;
  int32_t color;
  std::string module_data;
};

const std::unordered_map<std::string, ProposalContent> kNextStories(
    {{"Open Mail",
      {"file:///system/apps/email_story", 0x00aaaa00 /* yellow */, ""}},
     {"Spinning Square",
      {"file:///system/apps/color", 0x000000ff /*blue*/, ""}},
     {"Teal A400", {"file:///system/apps/color", 0x00ffffff, "0xFF1DE9B6"}}});

const std::unordered_map<std::string, ProposalContent> kAskOnlyStories(
    {{"Terminal", {"file:///system/apps/moterm", 0x00008000 /* green */, ""}},
     {"YouTube",
      {"file:///system/apps/youtube_story", 0x00ff0000 /* red */, ""}},
     {"Music",
      {"file:///system/apps/music_story", 0x00ff8000 /* orange */, ""}},
     {"Noodles", {"file:///system/apps/noodles_view", 0x00ffff00, ""}},
     {"Color", {"file:///system/apps/color", 0x00ffffff, ""}},
     {"Red 500", {"file:///system/apps/color", 0x00ffffff, "0xFFF44336"}},
     {"Deep Purple 800",
      {"file:///system/apps/color", 0x00ffffff, "0xFF4527A0"}},
     {"Green 500", {"file:///system/apps/color", 0x00ffffff, "0xFF4CAF50"}}});

maxwell::suggestion::ProposalPtr MkProposal(const std::string& label,
                                            const ProposalContent& content) {
  auto p = maxwell::suggestion::Proposal::New();

  p->id = label;
  auto create_story = maxwell::suggestion::CreateStory::New();
  create_story->module_id = content.url;
  const auto& data = content.module_data;
  if (data.size() > 0) {
    DocumentPtr doc(Document::New());
    doc->docid = label;
    // TODO(afergan): Don't hardcode the doc id key or initial_data map key.
    // TODO(afergan, azani): Right now we pass the colors as Strings because
    // document_store::Value does not support hexadecimal.
    doc->properties["Color"] = Value::New();
    doc->properties["Color"]->set_string_value(data);
    auto map = fidl::Map<fidl::String, DocumentPtr>();
    map[fidl::String("Color")] = std::move(doc);
    create_story->initial_data = std::move(map);
  }
  auto action = maxwell::suggestion::Action::New();
  action->set_create_story(std::move(create_story));
  p->on_selected.push_back(std::move(action));
  auto d = maxwell::suggestion::Display::New();
  d->headline = label;
  d->subheadline = "";
  d->details = "";
  d->color = content.color;
  d->icon_urls.push_back("");
  d->image_url = "";
  d->image_type = maxwell::suggestion::SuggestionImageType::PERSON;
  p->display = std::move(d);

  return p;
}

class ModuleSuggesterAgentApp : public maxwell::context::SubscriberLink,
                                public maxwell::suggestion::AskHandler {
 public:
  ModuleSuggesterAgentApp()
      : app_context_(modular::ApplicationContext::CreateFromStartupInfo()),
        maxwell_context_(app_context_->ConnectToEnvironmentService<
                         maxwell::context::SuggestionAgentClient>()),
        in_(this),
        out_(app_context_->ConnectToEnvironmentService<
             maxwell::suggestion::SuggestionAgentClient>()),
        ask_(this) {
    fidl::InterfaceHandle<maxwell::context::SubscriberLink> in_handle;
    in_.Bind(&in_handle);
    maxwell_context_->Subscribe("/modular_state", "int", std::move(in_handle));
    fidl::InterfaceHandle<maxwell::suggestion::AskHandler> ask_handle;
    ask_.Bind(&ask_handle);
    out_->RegisterAskHandler(std::move(ask_handle));
  }

  void OnUpdate(maxwell::context::UpdatePtr update) override {
    FTL_LOG(INFO) << "OnUpdate from " << update->source << ": "
                  << update->json_value;

    const int modular_state = std::stoi(update->json_value.data());

    if (modular_state == 0) {
      for (const auto& entry : kNextStories) {
        out_->Propose(MkProposal(entry.first, entry.second));
      }
    } else {
      for (const auto& entry : kNextStories) {
        out_->Remove(entry.first);
      }
    }
  }

  void Ask(maxwell::suggestion::UserInputPtr query,
           const AskCallback& callback) override {
    if (query->is_text() && query->get_text() != "") {
      // Propose everything; let the Next filter do the filtering HACK(rosswang)
      for (const auto& entry : kAskOnlyStories) {
        out_->Propose(MkProposal(entry.first, entry.second));
      }
    } else {
      for (const auto& entry : kAskOnlyStories) {
        out_->Remove(entry.first);
      }
    }

    callback(fidl::Array<maxwell::suggestion::ProposalPtr>::New(
        0));  // TODO(rosswang)
  }

 private:
  std::unique_ptr<modular::ApplicationContext> app_context_;

  maxwell::context::SuggestionAgentClientPtr maxwell_context_;
  fidl::Binding<maxwell::context::SubscriberLink> in_;
  maxwell::suggestion::SuggestionAgentClientPtr out_;
  fidl::Binding<maxwell::suggestion::AskHandler> ask_;
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  ModuleSuggesterAgentApp app;
  loop.Run();
  return 0;
}
