// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/suggestion_engine/suggestion_engine_impl.h"
#include "lib/app/cpp/application_context.h"
#include "apps/maxwell/services/suggestion/suggestion_engine.fidl.h"
#include "apps/maxwell/services/suggestion/user_input.fidl.h"
#include "apps/maxwell/src/suggestion_engine/ask_subscriber.h"
#include "apps/maxwell/src/suggestion_engine/interruptions_subscriber.h"
#include "apps/maxwell/src/suggestion_engine/next_subscriber.h"
#include "apps/modular/lib/fidl/json_xdr.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fsl/tasks/message_loop.h"

#include "lib/fidl/cpp/bindings/interface_ptr_set.h"

#include <string>

namespace maxwell {

bool IsInterruption(const SuggestionPrototype* suggestion) {
  return ((suggestion->proposal->display) &&
          ((suggestion->proposal->display->annoyance ==
            maxwell::AnnoyanceType::INTERRUPT) ||
           (suggestion->proposal->display->annoyance ==
            maxwell::AnnoyanceType::PEEK)));
}

SuggestionEngineImpl::SuggestionEngineImpl()
    : app_context_(app::ApplicationContext::CreateFromStartupInfo()),
      ask_suggestions_(new RankedSuggestions(&ask_channel_)),
      next_suggestions_(new RankedSuggestions(&next_channel_)) {
  app_context_->outgoing_services()->AddService<SuggestionEngine>(
      [this](fidl::InterfaceRequest<SuggestionEngine> request) {
        bindings_.AddBinding(this, std::move(request));
      });
  app_context_->outgoing_services()->AddService<SuggestionProvider>(
      [this](fidl::InterfaceRequest<SuggestionProvider> request) {
        suggestion_provider_bindings_.AddBinding(this, std::move(request));
      });
  app_context_->outgoing_services()->AddService<SuggestionDebug>(
      [this](fidl::InterfaceRequest<SuggestionDebug> request) {
        debug_bindings_.AddBinding(&debug_, std::move(request));
      });

  // The Next suggestions are always ranked with a static ranking function.
  next_suggestions_->UpdateRankingFunction(
      maxwell::ranking::GetNextRankingFunction());
}

void SuggestionEngineImpl::AddNextProposal(ProposalPublisherImpl* source,
                                           ProposalPtr proposal) {
  // The component_url and proposal ID form a unique identifier for a proposal.
  // If one already exists, remove it before adding the new one.
  RemoveProposal(source->component_url(), proposal->id);

  auto suggestion =
      CreateSuggestionPrototype(std::move(source), std::move(proposal));

  if (IsInterruption(suggestion)) {
    debug_.OnInterrupt(suggestion);
    // TODO(andrewosh): Subscribers should probably take SuggestionPrototypes.
    auto ranked_suggestion = new RankedSuggestion();
    ranked_suggestion->prototype = suggestion;
    ranked_suggestion->rank = 0;
    interruption_channel_.DispatchOnAddSuggestion(ranked_suggestion);
  }

  next_suggestions_->AddSuggestion(std::move(suggestion));
  debug_.OnNextUpdate(next_suggestions_);
}

void SuggestionEngineImpl::AddAskProposal(ProposalPublisherImpl* source,
                                          ProposalPtr proposal) {
  RemoveProposal(source->component_url(), proposal->id);
  auto suggestion =
      CreateSuggestionPrototype(std::move(source), std::move(proposal));
  ask_suggestions_->AddSuggestion(std::move(suggestion));
}

void SuggestionEngineImpl::RemoveProposal(const std::string& component_url,
                                          const std::string& proposal_id) {
  const auto key = std::make_pair(component_url, proposal_id);
  auto toRemove = suggestion_prototypes_.find(key);
  if (toRemove != suggestion_prototypes_.end()) {
    RankedSuggestion* matchingSuggestion =
        next_suggestions_->GetSuggestion(component_url, proposal_id);
    if (matchingSuggestion && IsInterruption(matchingSuggestion->prototype)) {
      interruption_channel_.DispatchOnRemoveSuggestion(matchingSuggestion);
    }
    ask_suggestions_->RemoveProposal(component_url, proposal_id);
    next_suggestions_->RemoveProposal(component_url, proposal_id);
    debug_.OnNextUpdate(next_suggestions_);
    suggestion_prototypes_.erase(toRemove);
  }
}

SuggestionPrototype* SuggestionEngineImpl::FindSuggestion(
    std::string suggestion_id) {
  RankedSuggestion* suggestion =
      next_suggestions_->GetSuggestion(suggestion_id);
  if (suggestion) {
    return suggestion->prototype;
  }
  suggestion = ask_suggestions_->GetSuggestion(suggestion_id);
  return suggestion->prototype;
}

// |AskDispatcher|
void SuggestionEngineImpl::DispatchAsk(UserInputPtr input) {
  // TODO(rosswang): locale/unicode
  std::string query = input->get_text();

  std::transform(query.begin(), query.end(), query.begin(), ::tolower);

  if (!query.empty()) {
    std::string formattedQuery;
    modular::XdrWrite(&formattedQuery, &query, modular::XdrFilter<std::string>);
    context_writer_->WriteEntityTopic(kQueryContextKey, formattedQuery);
  }

  // TODO(andrewosh): Include/exclude logic improves upon this, but with
  // increased complexity.
  RemoveAllAskSuggestions();

  ask_suggestions_->UpdateRankingFunction(
      maxwell::ranking::GetAskRankingFunction(query));

  if (ask_handlers_.size() == 0) {
    return debug_.OnAskStart(query, ask_suggestions_);
  }

  auto remainingHandlers = std::make_shared<size_t>(ask_handlers_.size());
  for (auto ask = ask_handlers_.begin(); ask != ask_handlers_.end(); ask++) {
    (*ask)->handler->Ask(
        input.Clone(),
        // TODO(andrewosh) Capturing the ask iterator is unsafe, as
        // ask_handlers_ can be modified during this iteration.
        // Replace ask_handlers_ with a map + validation when BoundPtrSet is
        // removed.
        [this, remainingHandlers, query,
         ask](fidl::Array<ProposalPtr> proposals) {
          for (auto& proposal : proposals) {
            AddAskProposal((*ask)->publisher.get(), std::move(proposal));
          }
          (*remainingHandlers)--;
          if ((*remainingHandlers) == 0) {
            debug_.OnAskStart(query, ask_suggestions_);
          }
        });
  }
}

// |SuggestionProvider|
void SuggestionEngineImpl::SubscribeToInterruptions(
    fidl::InterfaceHandle<SuggestionListener> listener) {
  InterruptionsSubscriber* subscriber =
      new InterruptionsSubscriber(std::move(listener));
  // New InterruptionsSubscribers are initially sent the existing set of Next
  // suggestions. AnnoyanceType filtering happens in the subscriber.
  for (const auto& suggestion : next_suggestions_->Get()) {
    subscriber->OnAddSuggestion(*suggestion);
  }
  interruption_channel_.AddSubscriber(std::move(subscriber));
}

// |SuggestionProvider|
void SuggestionEngineImpl::SubscribeToNext(
    fidl::InterfaceHandle<SuggestionListener> listener,
    fidl::InterfaceRequest<NextController> controller) {
  NextSubscriber* subscriber = new NextSubscriber(
      next_suggestions_, std::move(listener), std::move(controller));
  // New NextSubscribers are initially sent the existing set of Next
  // suggestions.
  for (const auto& suggestion : next_suggestions_->Get()) {
    subscriber->OnAddSuggestion(*suggestion);
  }
  next_channel_.AddSubscriber(std::move(subscriber));
}

// |SuggestionProvider|
void SuggestionEngineImpl::InitiateAsk(
    fidl::InterfaceHandle<SuggestionListener> listener,
    fidl::InterfaceRequest<AskController> controller) {
  AskSubscriber* subscriber = new AskSubscriber(
      ask_suggestions_, this, std::move(listener), std::move(controller));
  ask_channel_.AddSubscriber(std::move(subscriber));
}

// |SuggestionProvider|
void SuggestionEngineImpl::NotifyInteraction(
    const fidl::String& suggestion_uuid,
    InteractionPtr interaction) {
  SuggestionPrototype* suggestion_prototype = FindSuggestion(suggestion_uuid);

  std::string log_detail = suggestion_prototype
                               ? short_proposal_str(*suggestion_prototype)
                               : "invalid";

  FXL_LOG(INFO) << (interaction->type == InteractionType::SELECTED
                        ? "Accepted"
                        : "Dismissed")
                << " suggestion " << suggestion_uuid << " (" << log_detail
                << ")";

  debug_.OnSuggestionSelected(suggestion_prototype);

  if (suggestion_prototype) {
    auto& proposal = suggestion_prototype->proposal;
    if (interaction->type == InteractionType::SELECTED) {
      PerformActions(proposal->on_selected, proposal->display->color);
      RemoveProposal(suggestion_prototype->source_url, proposal->id);
    }
  }
}

// |SuggestionEngine|
void SuggestionEngineImpl::RegisterPublisher(
    const fidl::String& url,
    fidl::InterfaceRequest<ProposalPublisher> client) {
  GetOrCreateSourceClient(url)->AddBinding(std::move(client));
}

// |SuggestionEngine|
void SuggestionEngineImpl::Initialize(
    fidl::InterfaceHandle<modular::StoryProvider> story_provider,
    fidl::InterfaceHandle<modular::FocusProvider> focus_provider,
    fidl::InterfaceHandle<ContextWriter> context_writer) {
  story_provider_.Bind(std::move(story_provider));
  focus_provider_ptr_.Bind(std::move(focus_provider));
  context_writer_.Bind(std::move(context_writer));

  timeline_stories_watcher_.reset(new TimelineStoriesWatcher(&story_provider_));
}

// end SuggestionEngine

void SuggestionEngineImpl::RemoveAllAskSuggestions() {
  for (auto& suggestion : ask_suggestions_->Get()) {
    suggestion_prototypes_.erase(
        std::make_pair(suggestion->prototype->source_url,
                       suggestion->prototype->proposal->id));
  }
  ask_suggestions_->RemoveAllSuggestions();
}

SuggestionPrototype* SuggestionEngineImpl::CreateSuggestionPrototype(
    ProposalPublisherImpl* source,
    ProposalPtr proposal) {
  auto prototype_pair = suggestion_prototypes_.emplace(
      std::make_pair(source->component_url(), proposal->id),
      std::make_unique<SuggestionPrototype>());
  auto suggestion_prototype = prototype_pair.first->second.get();
  suggestion_prototype->suggestion_id = RandomUuid();
  suggestion_prototype->source_url = source->component_url();
  suggestion_prototype->timestamp = fxl::TimePoint::Now();
  suggestion_prototype->proposal = std::move(proposal);

  return suggestion_prototype;
}

ProposalPublisherImpl* SuggestionEngineImpl::GetOrCreateSourceClient(
    const std::string& component_url) {
  std::unique_ptr<ProposalPublisherImpl>& source =
      proposal_publishers_[component_url];
  if (!source)  // create if it didn't already exist
    source.reset(new ProposalPublisherImpl(this, component_url));

  return source.get();
}

void SuggestionEngineImpl::AddAskPublisher(
    std::unique_ptr<AskPublisher> publisher) {
  ask_handlers_.emplace(std::move(publisher));
}

void SuggestionEngineImpl::PerformActions(
    const fidl::Array<maxwell::ActionPtr>& actions,
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
                FXL_LOG(INFO) << "Creating story with module " << module_id;

                story_controller->GetInfo(fxl::MakeCopyable(
                    // TODO(thatguy): We should not be std::move()ing
                    // story_controller *while we're calling it*.
                    [ this, controller = std::move(story_controller) ](
                        modular::StoryInfoPtr story_info,
                        modular::StoryState state) {
                      FXL_LOG(INFO)
                          << "Requesting focus for story_id " << story_info->id;
                      focus_provider_ptr_->Request(story_info->id);
                    }));
              });
        } else {
          FXL_LOG(WARNING) << "Unable to add module; no story provider";
        }
        break;
      }
      case Action::Tag::FOCUS_STORY: {
        const auto& focus_story = action->get_focus_story();
        FXL_LOG(INFO) << "Requesting focus for story_id "
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
          const auto& surface_relation = add_module_to_story->surface_relation;

          FXL_LOG(INFO) << "Adding module " << module_url << " to story "
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
                                      module_url, link_name,
                                      surface_relation.Clone());
        } else {
          FXL_LOG(WARNING) << "Unable to add module; no story provider";
        }

        break;
      }
      case Action::Tag::CUSTOM_ACTION: {
        auto custom_action = maxwell::CustomActionPtr::Create(
            std::move(action->get_custom_action()));
        custom_action->Execute(fxl::MakeCopyable([
          this, custom_action = std::move(custom_action), story_color
        ](fidl::Array<maxwell::ActionPtr> actions) {
          if (actions)
            PerformActions(std::move(actions), story_color);
        }));
        break;
      }
      default:
        FXL_LOG(WARNING) << "Unknown action tag " << (uint32_t)action->which();
    }
  }
}

}  // namespace maxwell

int main(int argc, const char** argv) {
  fsl::MessageLoop loop;
  maxwell::SuggestionEngineImpl app;
  loop.Run();
  return 0;
}
