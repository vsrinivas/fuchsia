// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/services/suggestion/suggestion_engine.fidl.h"
#include "apps/maxwell/src/bound_set.h"
#include "apps/maxwell/src/suggestion_engine/next_subscriber.h"
#include "apps/maxwell/src/suggestion_engine/repo.h"
#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/services/user/story_provider.fidl.h"
#include "lib/mtl/tasks/message_loop.h"

namespace maxwell {
namespace suggestion {

class SuggestionEngineApp : public SuggestionEngine, public SuggestionProvider {
 public:
  SuggestionEngineApp()
      : app_context_(modular::ApplicationContext::CreateFromStartupInfo()) {
    app_context_->outgoing_services()->AddService<SuggestionEngine>(
        [this](fidl::InterfaceRequest<SuggestionEngine> request) {
          bindings_.AddBinding(this, std::move(request));
        });
    app_context_->outgoing_services()->AddService<SuggestionProvider>(
        [this](fidl::InterfaceRequest<SuggestionProvider> request) {
          suggestion_provider_bindings_.AddBinding(this, std::move(request));
        });
  }

  // SuggestionProvider

  void SubscribeToInterruptions(fidl::InterfaceHandle<Listener> listener) {
    // TODO(rosswang): no interruptions yet
  }

  void SubscribeToNext(fidl::InterfaceHandle<Listener> listener,
                       fidl::InterfaceRequest<NextController> controller) {
    std::unique_ptr<NextSubscriber> sub(new NextSubscriber(
        repo_.next_ranked_suggestions(), std::move(listener)));
    sub->Bind(std::move(controller));
    repo_.AddNextSubscriber(std::move(sub));
  }

  void InitiateAsk(fidl::InterfaceHandle<Listener> listener,
                   fidl::InterfaceRequest<AskController> controller) {
    // TODO(rosswang): no ask handlers yet
  }

  void NotifyInteraction(const fidl::String& suggestion_uuid,
                         InteractionPtr interaction) {
    const ProposalRecord* proposal_record = repo_[suggestion_uuid];

    std::ostringstream log_detail;
    if (proposal_record)
      log_detail << "proposal " << proposal_record->proposal->id << " from "
                 << proposal_record->source->component_url();
    else
      log_detail << "invalid";

    FTL_LOG(INFO) << (interaction->type == InteractionType::SELECTED
                          ? "Accepted"
                          : "Dismissed")
                  << " suggestion " << suggestion_uuid << " ("
                  << log_detail.str() << ")";

    if (proposal_record) {
      if (interaction->type == InteractionType::SELECTED) {
        modular::StoryControllerPtr story;
        // TODO(rosswang): If we're asked to add multiple modules, we probably
        // want to add them to the same story. We can't do that yet, but we need
        // to receive a StoryController anyway (not optional atm.).
        for (const auto& action : proposal_record->proposal->on_selected) {
          switch (action->which()) {
            case Action::Tag::CREATE_STORY: {
              const auto& create_story = action->get_create_story();
              if (story_provider_) {
                story_provider_->CreateStory(create_story->module_id,
                                             GetProxy(&story));
                FTL_LOG(INFO) << "Creating story with module "
                              << create_story->module_id;
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

      proposal_record->source->Remove(proposal_record->proposal->id);
    }
  }

  // end SuggestionProvider

  // SuggestionEngine

  void RegisterSuggestionAgent(
      const fidl::String& url,
      fidl::InterfaceRequest<SuggestionAgentClient> client) {
    repo_.GetOrCreateSourceClient(url)->AddBinding(std::move(client));
  }

  void SetStoryProvider(
      fidl::InterfaceHandle<modular::StoryProvider> story_provider) {
    story_provider_.Bind(std::move(story_provider));
  }

  // end SuggestionEngine

 private:
  std::unique_ptr<modular::ApplicationContext> app_context_;

  fidl::BindingSet<SuggestionEngine> bindings_;
  fidl::BindingSet<SuggestionProvider> suggestion_provider_bindings_;

  modular::StoryProviderPtr story_provider_;

  Repo repo_;
};

}  // suggestion
}  // maxwell

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  maxwell::suggestion::SuggestionEngineApp app;
  loop.Run();
  return 0;
}
