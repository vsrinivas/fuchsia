// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unordered_map>

#include "application/lib/app/application_context.h"
#include "apps/maxwell/services/suggestion/proposal_publisher.fidl.h"
#include "apps/modular/lib/rapidjson/rapidjson.h"
#include "lib/mtl/tasks/message_loop.h"
#include "third_party/rapidjson/rapidjson/document.h"
#include "third_party/rapidjson/rapidjson/pointer.h"

namespace maxwell {
namespace {

struct ProposalContent {
  std::string url;
  uint32_t color;
  std::string module_data;
  std::string icon;
};

const std::unordered_map<std::string, ProposalContent> kAskOnlyStories(
    {{"Terminal",
      {"moterm", 0xff212121 /*Grey 900*/, "", ""}},
     {"YouTube",
      {"youtube_story",
       0xffe52d27 /*YouTube red from color spec*/, "",
       "http://s-media-cache-ak0.pinimg.com/originals/bf/66/4b/"
       "bf664b1b730ac0423225c0c3526a44ef.jpg"}},
     {"Noodles", {"noodles_view", 0xff212121 /*Grey 900*/, "", ""}},
     {"Color", {"color", 0xff5affd6 /*Custom turquoise*/, "", ""}},
     {"Spinning Square", {"spinning_square_view",
       0xff512da8 /*Deep Purple 700*/, "", ""}},
     {"Paint", {"paint_view", 0xffad1457 /*Blue Grey 50*/, "", ""}},
     {"Hello Material", {"hello_material", 0xff4caf50 /*Green 500*/, "", ""}},
     {"Teal A400", {"color", 0xff1de9b6, "0xFF1DE9B6", ""}},
     {"Red 500", {"color", 0xfff44336, "0xFFF44336", ""}},
     {"Deep Purple 800", {"color", 0xff4527a0, "0xFF4527A0", ""}},
     {"Green 500", {"color", 0xff4caf50, "0xFF4CAF50", ""}}});

ProposalPtr MkProposal(const std::string& label,
                       const ProposalContent& content) {
  auto p = Proposal::New();

  p->id = label;
  auto create_story = CreateStory::New();
  create_story->module_id = content.url;
  const auto& data = content.module_data;
  if (data.size() > 0) {
    // TODO(afergan): Don't hardcode the doc id key or initial_data map key.
    rapidjson::Document doc;
    std::vector<std::string> segments{"color"};
    auto pointer =
        modular::CreatePointer(doc, segments.begin(), segments.end());
    pointer.Set(doc, data);
    create_story->initial_data = modular::JsonValueToString(doc);
  }
  auto action = Action::New();
  action->set_create_story(std::move(create_story));
  p->on_selected.push_back(std::move(action));
  auto d = SuggestionDisplay::New();
  d->headline = label;
  d->subheadline = "";
  d->details = "";
  d->color = content.color;
  d->icon_urls.push_back("");
  d->image_url = content.icon;
  d->image_type = SuggestionImageType::OTHER;
  p->display = std::move(d);

  return p;
}

class ModuleSuggesterAgentApp : public AskHandler {
 public:
  ModuleSuggesterAgentApp()
      : app_context_(app::ApplicationContext::CreateFromStartupInfo()),
        out_(app_context_->ConnectToEnvironmentService<ProposalPublisher>()),
        ask_(this) {
    fidl::InterfaceHandle<AskHandler> ask_handle;
    ask_.Bind(&ask_handle);
    out_->RegisterAskHandler(std::move(ask_handle));
  }

  void Ask(UserInputPtr query, const AskCallback& callback) override {
    if (query->is_text() && query->get_text().size() >= 4) {
      // Propose everything; let the Next filter do the filtering
      // HACK(rosswang)
      for (const auto& entry : kAskOnlyStories) {
        out_->Propose(MkProposal(entry.first, entry.second));
      }
    } else {
      for (const auto& entry : kAskOnlyStories) {
        out_->Remove(entry.first);
      }
    }

    callback(fidl::Array<ProposalPtr>::New(0));  // TODO(rosswang)
  }

 private:
  std::unique_ptr<app::ApplicationContext> app_context_;

  ProposalPublisherPtr out_;
  fidl::Binding<AskHandler> ask_;
};

}  // namespace
}  // namespace maxwell

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  maxwell::ModuleSuggesterAgentApp app;
  loop.Run();
  return 0;
}
