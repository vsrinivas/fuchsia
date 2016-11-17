// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/suggestion_engine/suggestion_engine.h"

#include "lib/mtl/tasks/message_loop.h"

namespace maxwell {
namespace suggestion {

// SuggestionProvider

void SuggestionEngineApp::SubscribeToInterruptions(
    fidl::InterfaceHandle<Listener> listener) {
  // TODO(rosswang): no interruptions yet
}

void SuggestionEngineApp::SubscribeToNext(
    fidl::InterfaceHandle<Listener> listener,
    fidl::InterfaceRequest<NextController> controller) {
  std::unique_ptr<NextSubscriber> sub(
      new NextSubscriber(&ranked_suggestions_, std::move(listener)));
  sub->Bind(std::move(controller));
  next_subscribers_.emplace(std::move(sub));
}

void SuggestionEngineApp::InitiateAsk(
    fidl::InterfaceHandle<Listener> listener,
    fidl::InterfaceRequest<AskController> controller) {
  // TODO(rosswang): no ask handlers yet
}

void SuggestionEngineApp::NotifyInteraction(const fidl::String& suggestion_uuid,
                                            InteractionPtr interaction) {
  SuggestionRecord* suggestion_record = suggestions_[suggestion_uuid];

  std::ostringstream log_detail;
  if (suggestion_record)
    log_detail << "proposal " << suggestion_record->proposal->id << " from "
               << suggestion_record->source->component_url();
  else
    log_detail << "invalid";

  FTL_LOG(INFO) << (interaction->type == InteractionType::SELECTED
                        ? "Accepted"
                        : "Dismissed")
                << " suggestion " << suggestion_uuid << " (" << log_detail.str()
                << ")";

  if (suggestion_record) {
    if (interaction->type == InteractionType::SELECTED) {
      modular::StoryControllerPtr story;
      // TODO(rosswang): If we're asked to add multiple modules, we probably
      // want to add them to the same story. We can't do that yet, but we need
      // to receive a StoryController anyway (not optional atm.).
      for (const auto& action : suggestion_record->proposal->on_selected) {
        switch (action->which()) {
          case Action::Tag::ADD_MODULE: {
            const auto& add_module = action->get_add_module();
            if (story_provider_) {
              story_provider_->CreateStory(add_module->module_id,
                                           GetProxy(&story));
              FTL_LOG(INFO) << "Adding module " << add_module->module_id;
            } else {
              FTL_LOG(WARNING) << "Unable to add module; no story provider";
            }
            break;
          }
          default:
            FTL_LOG(WARNING) << "Unknown action tag "
                             << (uint32_t)action->which();
        }
      }
    }

    suggestion_record->source->Remove(suggestion_record->proposal->id);
  }
}

// end SuggestionProvider

// SuggestionEngine

void SuggestionEngineApp::RegisterSuggestionAgent(
    const fidl::String& url,
    fidl::InterfaceRequest<SuggestionAgentClient> client) {
  std::unique_ptr<SuggestionAgentClientImpl>& source = sources_[url];
  if (!source)  // create if it didn't already exist
    source.reset(new SuggestionAgentClientImpl(this, url));

  source->AddBinding(std::move(client));
}

void SuggestionEngineApp::SetStoryProvider(
    fidl::InterfaceHandle<modular::StoryProvider> story_provider) {
  story_provider_.Bind(std::move(story_provider));
}

// end SuggestionEngine

}  // suggestion
}  // maxwell

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  maxwell::suggestion::SuggestionEngineApp app;
  loop.Run();
  return 0;
}
