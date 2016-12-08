// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unordered_map>

#include "apps/maxwell/services/context/client.fidl.h"
#include "apps/maxwell/services/suggestion/proposal_publisher.fidl.h"
#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/src/document_store/documents.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

struct ProposalContent {
  std::string url;
  uint32_t color;
  std::string module_data;
  std::string icon;
};

const std::unordered_map<std::string, ProposalContent> kNextStories(
    {{"Open Mail",
      {"file:///system/apps/email_story", 0xff4285f4 /*Blue from Inbox*/, "",
       ""}},
     {"Video Player",
      {"file:///system/apps/video_player", 0xff9575cd /*Deep Purple 300*/, "",
       ""}}});

const std::unordered_map<std::string, ProposalContent> kAskOnlyStories(
    {{"Terminal",
      {"file:///system/apps/moterm", 0xff212121 /*Grey 900*/, "", ""}},
     {"YouTube",
      {"file:///system/apps/youtube_story",
       0xffe52d27 /*YouTube red from color spec*/, "",
       "http://s-media-cache-ak0.pinimg.com/originals/bf/66/4b/"
       "bf664b1b730ac0423225c0c3526a44ef.jpg"}},
     {"Music",
      {"file:///system/apps/music_story",
       0xffff8c00 /*Google Play Music logo orange*/, "",
       "http://www.technobuffalo.com/wp-content/uploads/2016/03/"
       "kanye-west-1280x640.jpg"}},
     {"Noodles",
      {"file:///system/apps/noodles_view", 0xff212121 /*Grey 900*/, "", ""}},
     {"Color",
      {"file:///system/apps/color", 0xff5affd6 /*Custom turquoise*/, "", ""}},
     {"Spinning Square",
      {"file:///system/apps/spinning_square_view",
       0xff512da8 /*Deep Purple 700*/, "", ""}},
     {"Paint",
      {"file:///system/apps/paint_view", 0xffad1457 /*Blue Grey 50*/, "", ""}},
     {"Hello Material",
      {"file:///system/apps/hello_material", 0xff4caf50 /*Green 500*/, "", ""}},
     {"Teal A400", {"file:///system/apps/color", 0xff1de9b6, "0xFF1DE9B6", ""}},
     {"Red 500", {"file:///system/apps/color", 0xfff44336, "0xFFF44336", ""}},
     {"Deep Purple 800",
      {"file:///system/apps/color", 0xff4527a0, "0xFF4527A0", ""}},
     {"Green 500",
      {"file:///system/apps/color", 0xff4caf50, "0xFF4CAF50", ""}}});

maxwell::ProposalPtr MkProposal(const std::string& label,
                                const ProposalContent& content) {
  auto p = maxwell::Proposal::New();

  p->id = label;
  auto create_story = maxwell::CreateStory::New();
  create_story->module_id = content.url;
  const auto& data = content.module_data;
  if (data.size() > 0) {
    auto doc = document_store::Document::New();
    doc->docid = label;
    // TODO(afergan): Don't hardcode the doc id key or initial_data map key.
    // TODO(afergan, azani): Right now we pass the colors as Strings because
    // document_store::Value does not support hexadecimal.
    doc->properties["Color"] = document_store::Value::New();
    doc->properties["Color"]->set_string_value(data);
    fidl::Map<fidl::String, document_store::DocumentPtr> map;
    map[fidl::String("Color")] = std::move(doc);
    create_story->initial_data = std::move(map);
  }
  auto action = maxwell::Action::New();
  action->set_create_story(std::move(create_story));
  p->on_selected.push_back(std::move(action));
  auto d = maxwell::SuggestionDisplay::New();
  d->headline = label;
  d->subheadline = "";
  d->details = "";
  d->color = content.color;
  d->icon_urls.push_back("");
  d->image_url = content.icon;
  d->image_type = maxwell::SuggestionImageType::OTHER;
  p->display = std::move(d);

  return p;
}

class ModuleSuggesterAgentApp : public maxwell::ContextSubscriberLink,
                                public maxwell::AskHandler {
 public:
  ModuleSuggesterAgentApp()
      : app_context_(modular::ApplicationContext::CreateFromStartupInfo()),
        maxwell_context_(
            app_context_
                ->ConnectToEnvironmentService<maxwell::ContextSubscriber>()),
        in_(this),
        out_(app_context_
                 ->ConnectToEnvironmentService<maxwell::ProposalPublisher>()),
        ask_(this) {
    fidl::InterfaceHandle<maxwell::ContextSubscriberLink> in_handle;
    in_.Bind(&in_handle);
    maxwell_context_->Subscribe("/modular_state", "int", std::move(in_handle));
    fidl::InterfaceHandle<maxwell::AskHandler> ask_handle;
    ask_.Bind(&ask_handle);
    out_->RegisterAskHandler(std::move(ask_handle));
  }

  void OnUpdate(maxwell::ContextUpdatePtr update) override {
    const int modular_state = std::stoi(update->json_value.data());
    FTL_LOG(INFO) << "OnUpdate from " << update->source << ": "
                  << update->json_value << " = (int) " << modular_state;

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

  void Ask(maxwell::UserInputPtr query, const AskCallback& callback) override {
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

    callback(fidl::Array<maxwell::ProposalPtr>::New(0));  // TODO(rosswang)
  }

 private:
  std::unique_ptr<modular::ApplicationContext> app_context_;

  maxwell::ContextSubscriberPtr maxwell_context_;
  fidl::Binding<maxwell::ContextSubscriberLink> in_;
  maxwell::ProposalPublisherPtr out_;
  fidl::Binding<maxwell::AskHandler> ask_;
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  ModuleSuggesterAgentApp app;
  loop.Run();
  return 0;
}
