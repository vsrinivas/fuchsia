// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/app/application_context.h"
#include "apps/maxwell/services/context/context_publisher.fidl.h"
#include "apps/maxwell/services/suggestion/suggestion_engine.fidl.h"
#include "apps/maxwell/src/bound_set.h"
#include "apps/maxwell/src/suggestion_engine/ask_subscriber.h"
#include "apps/maxwell/src/suggestion_engine/next_subscriber.h"
#include "apps/maxwell/src/suggestion_engine/repo.h"
#include "apps/maxwell/src/suggestion_engine/timeline_stories_filter.h"
#include "apps/maxwell/src/suggestion_engine/timeline_stories_watcher.h"
#include "apps/modular/services/story/story_provider.fidl.h"
#include "apps/modular/services/user/focus.fidl.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/mtl/tasks/message_loop.h"

namespace maxwell {

class SuggestionEngineApp : public SuggestionEngine, public SuggestionProvider {
 public:
  SuggestionEngineApp()
      : app_context_(app::ApplicationContext::CreateFromStartupInfo()) {
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
      fidl::InterfaceHandle<SuggestionListener> listener) override {
    repo_->SubscribeToInterruptions(std::move(listener));
  }

  void SubscribeToNext(
      fidl::InterfaceHandle<SuggestionListener> listener,
      fidl::InterfaceRequest<NextController> controller) override {
    repo_->SubscribeToNext(std::move(listener), std::move(controller));
  }

  void InitiateAsk(fidl::InterfaceHandle<SuggestionListener> listener,
                   fidl::InterfaceRequest<AskController> controller) override {
    repo_->InitiateAsk(std::move(listener), std::move(controller));
  }

  void NotifyInteraction(const fidl::String& suggestion_uuid,
                         InteractionPtr interaction) override {
    std::unique_ptr<SuggestionPrototype> suggestion_prototype =
        repo_->Extract(suggestion_uuid);

    std::string log_detail = suggestion_prototype
                                 ? short_proposal_str(*suggestion_prototype)
                                 : "invalid";

    FTL_LOG(INFO) << (interaction->type == InteractionType::SELECTED
                          ? "Accepted"
                          : "Dismissed")
                  << " suggestion " << suggestion_uuid << " (" << log_detail
                  << ")";

    if (suggestion_prototype) {
      if (interaction->type == InteractionType::SELECTED) {
        PerformActions(suggestion_prototype->proposal->on_selected,
                       suggestion_prototype->proposal->display->color);
      }
    }
  }

  // end SuggestionProvider

  // SuggestionEngine

  void RegisterPublisher(
      const fidl::String& url,
      fidl::InterfaceRequest<ProposalPublisher> client) override {
    repo_->GetOrCreateSourceClient(url)->AddBinding(std::move(client));
  }

  void Initialize(
      fidl::InterfaceHandle<modular::StoryProvider> story_provider,
      fidl::InterfaceHandle<modular::FocusProvider> focus_provider,
      fidl::InterfaceHandle<ContextPublisher> context_publisher) override {
    story_provider_.Bind(std::move(story_provider));
    focus_provider_ptr_.Bind(std::move(focus_provider));

    ContextPublisherPtr context_publisher_ptr;
    context_publisher_ptr.Bind(std::move(context_publisher));

    timeline_stories_watcher_.reset(
        new TimelineStoriesWatcher(&story_provider_));
    // timeline_stories_watcher_->SetWatcher(
    //     []() { FTL_LOG(INFO) << "Something changed."; });

    repo_.reset(
        new Repo(TimelineStoriesFilter(timeline_stories_watcher_.get()),
                 std::move(context_publisher_ptr)));
  }

  // end SuggestionEngine

 private:
  void PerformActions(const fidl::Array<maxwell::ActionPtr>& actions,
                      uint32_t story_color) {
    // TODO(rosswang): If we're asked to add multiple modules, we probably
    // want to add them to the same story. We can't do that yet, but we need
    // to receive a StoryController anyway (not optional atm.).
    for (const auto& action : actions) {
      switch (action->which()) {
        case Action::Tag::CREATE_STORY: {
          const auto& create_story = action->get_create_story();

          if (story_provider_) {
            // TODO(afergan): Make this more robust later. For now, we
            // always assume that there's extra info and that it's a color.
            fidl::Map<fidl::String, fidl::String> extra_info;
            char hex_color[11];
            sprintf(hex_color, "0x%x", story_color);
            extra_info["color"] = hex_color;
            auto& initial_data = create_story->initial_data;
            auto& module_id = create_story->module_id;
            story_provider_->CreateStoryWithInfo(
                create_story->module_id, std::move(extra_info),
                std::move(initial_data),
                [this, module_id](const fidl::String& story_id) {
                  modular::StoryControllerPtr story_controller;
                  story_provider_->GetController(story_id,
                                                 story_controller.NewRequest());
                  FTL_LOG(INFO) << "Creating story with module " << module_id;

                  story_controller->GetInfo(ftl::MakeCopyable(
                      // TODO(thatguy): We should not be std::move()ing
                      // story_controller *while we're calling it*.
                      [ this, controller = std::move(story_controller) ](
                          modular::StoryInfoPtr story_info,
                          modular::StoryState state) {
                        FTL_LOG(INFO) << "Requesting focus for story_id "
                                      << story_info->id;
                        focus_provider_ptr_->Request(story_info->id);
                      }));
                });
          } else {
            FTL_LOG(WARNING) << "Unable to add module; no story provider";
          }
          break;
        }
        case Action::Tag::FOCUS_STORY: {
          const auto& focus_story = action->get_focus_story();
          FTL_LOG(INFO) << "Requesting focus for story_id "
                        << focus_story->story_id;
          focus_provider_ptr_->Request(focus_story->story_id);
          break;
        }
        case Action::Tag::ADD_MODULE_TO_STORY: {
          if (story_provider_) {
            const auto& add_module_to_story = action->get_add_module_to_story();
            const auto& story_id = add_module_to_story->story_id;
            const auto& module_name = add_module_to_story->module_name;
            const auto& module_url = add_module_to_story->module_url;
            const auto& link_name = add_module_to_story->link_name;
            const auto& module_path = add_module_to_story->module_path;

            FTL_LOG(INFO) << "Adding module " << module_url << " to story "
                          << story_id;

            modular::StoryControllerPtr story_controller;
            story_provider_->GetController(story_id,
                                           story_controller.NewRequest());
            if (!add_module_to_story->initial_data.is_null()) {
              modular::LinkPtr link;
              story_controller->GetLink(module_path.Clone(), link_name,
                                        link.NewRequest());
              link->Set(nullptr /* json_path */,
                        add_module_to_story->initial_data);
            }

            story_controller->AddModule(module_path.Clone(), module_name,
                                        module_url, link_name);
            FTL_LOG(INFO) << "Requesting focus for story_id " << story_id;
            focus_provider_ptr_->Request(story_id);
          } else {
            FTL_LOG(WARNING) << "Unable to add module; no story provider";
          }

          break;
        }
        case Action::Tag::CUSTOM_ACTION: {
          auto custom_action = maxwell::CustomActionPtr::Create(
              std::move(action->get_custom_action()));
          custom_action->Execute(ftl::MakeCopyable([
            this, custom_action = std::move(custom_action), story_color
          ](fidl::Array<maxwell::ActionPtr> actions) {
            if (actions)
              PerformActions(std::move(actions), story_color);
          }));
          break;
        }
        default:
          FTL_LOG(WARNING) << "Unknown action tag "
                           << (uint32_t)action->which();
      }
    }
  }

  std::unique_ptr<app::ApplicationContext> app_context_;

  fidl::BindingSet<SuggestionEngine> bindings_;
  fidl::BindingSet<SuggestionProvider> suggestion_provider_bindings_;

  modular::StoryProviderPtr story_provider_;
  fidl::InterfacePtr<modular::FocusProvider> focus_provider_ptr_;

  ContextPublisherPtr context_publisher_;

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

}  // namespace maxwell

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  maxwell::SuggestionEngineApp app;
  loop.Run();
  return 0;
}
