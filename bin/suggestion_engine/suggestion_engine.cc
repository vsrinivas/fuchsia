// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/services/suggestion/suggestion_engine.fidl.h"
#include "apps/maxwell/src/bound_set.h"
#include "apps/maxwell/src/suggestion_engine/ask_subscriber.h"
#include "apps/maxwell/src/suggestion_engine/next_subscriber.h"
#include "apps/maxwell/src/suggestion_engine/repo.h"
#include "apps/maxwell/src/suggestion_engine/timeline_stories_filter.h"
#include "apps/maxwell/src/suggestion_engine/timeline_stories_watcher.h"
#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/services/user/focus.fidl.h"
#include "apps/modular/services/user/story_provider.fidl.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/ftl/functional/make_copyable.h"

namespace maxwell {

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

  void SubscribeToInterruptions(
      fidl::InterfaceHandle<Listener> listener) override {
    // TODO(rosswang): no interruptions yet
  }

  void SubscribeToNext(
      fidl::InterfaceHandle<Listener> listener,
      fidl::InterfaceRequest<NextController> controller) override {
    repo_->SubscribeToNext(std::move(listener), std::move(controller));
  }

  void InitiateAsk(fidl::InterfaceHandle<Listener> listener,
                   fidl::InterfaceRequest<AskController> controller) override {
    repo_->InitiateAsk(std::move(listener), std::move(controller));
  }

  void NotifyInteraction(const fidl::String& suggestion_uuid,
                         InteractionPtr interaction) override {
    const ProposalRecord* proposal_record = (*repo_)[suggestion_uuid];

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
        // TODO(rosswang): If we're asked to add multiple modules, we probably
        // want to add them to the same story. We can't do that yet, but we need
        // to receive a StoryController anyway (not optional atm.).
        for (const auto& action : proposal_record->proposal->on_selected) {
          switch (action->which()) {
            case Action::Tag::CREATE_STORY: {
              modular::StoryControllerPtr story_controller;
              const auto& create_story = action->get_create_story();
              if (story_provider_) {
                story_provider_->CreateStory(create_story->module_id,
                                             story_controller.NewRequest());
                FTL_LOG(INFO) << "Creating story with module "
                              << create_story->module_id;
                char hex_color[11];
                sprintf(hex_color, "0x%x",
                        proposal_record->proposal->display->color);
                story_controller->SetInfoExtra(fidl::String("color"),
                                               fidl::String(hex_color), [] {});
                const auto& initial_data = create_story->initial_data;
                if (initial_data) {
                  modular::LinkPtr link;
                  // TODO(afergan): This won't work until CreateStory() supports
                  // initial Link data (FW-66).
                  story_controller->GetLink(link.NewRequest());
                  link->AddDocuments(initial_data.Clone());
                }
                story_controller->GetInfo(ftl::MakeCopyable(
                    // TODO(thatguy): We should not be std::move()ing
                    // story_controller *while we're calling it*.
                    [ this, controller = std::move(story_controller) ](
                        modular::StoryInfoPtr story_info) {
                      FTL_LOG(INFO) << "Focusing!";
                      focus_controller_ptr_->FocusStory(story_info->id);
                    }));
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
      fidl::InterfaceRequest<ProposalPublisher> client) override {
    repo_->GetOrCreateSourceClient(url)->AddBinding(std::move(client));
  }

  void Initialize(fidl::InterfaceHandle<modular::StoryProvider> story_provider,
                  fidl::InterfaceHandle<modular::FocusController>
                      focus_controller) override {
    story_provider_.Bind(std::move(story_provider));
    focus_controller_ptr_.Bind(std::move(focus_controller));

    timeline_stories_watcher_.reset(
        new TimelineStoriesWatcher(&story_provider_));
    // timeline_stories_watcher_->SetWatcher(
    //     []() { FTL_LOG(INFO) << "Something changed."; });

    repo_.reset(
        new Repo(TimelineStoriesFilter(timeline_stories_watcher_.get())));
  }

  // end SuggestionEngine

 private:
  std::unique_ptr<modular::ApplicationContext> app_context_;

  fidl::BindingSet<SuggestionEngine> bindings_;
  fidl::BindingSet<SuggestionProvider> suggestion_provider_bindings_;

  modular::StoryProviderPtr story_provider_;
  fidl::InterfacePtr<modular::FocusController> focus_controller_ptr_;

  // Watches for changes in StoryInfo from the StoryProvider, acts as a filter
  // for Proposals on all channels, and notifies when there are changes so that
  // we can re-filter Proposals.
  //
  // Initialized late in Initialize().
  std::unique_ptr<TimelineStoriesWatcher> timeline_stories_watcher_;

  // TODO(thatguy): All Channels also get a ReevaluateFilters method, which
  // would remove Suggestions that are now filtered or add
  // new ones that are no longer filtered.

  std::unique_ptr<Repo> repo_;
};

}  // maxwell

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  maxwell::SuggestionEngineApp app;
  loop.Run();
  return 0;
}
