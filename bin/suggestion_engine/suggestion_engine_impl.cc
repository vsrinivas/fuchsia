// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/suggestion_engine_impl.h"

#include <fuchsia/cpp/modular.h>
#include <string>

#include "lib/context/cpp/context_helper.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/time/time_delta.h"
#include "lib/fxl/time/time_point.h"

#include "peridot/bin/suggestion_engine/auto_select_first_query_listener.h"
#include "peridot/bin/suggestion_engine/rankers/linear_ranker.h"
#include "peridot/bin/suggestion_engine/ranking_feature.h"
#include "peridot/bin/suggestion_engine/ranking_features/focused_story_ranking_feature.h"
#include "peridot/bin/suggestion_engine/ranking_features/kronk_ranking_feature.h"
#include "peridot/bin/suggestion_engine/ranking_features/mod_pair_ranking_feature.h"
#include "peridot/bin/suggestion_engine/ranking_features/proposal_hint_ranking_feature.h"
#include "peridot/bin/suggestion_engine/ranking_features/query_match_ranking_feature.h"
#include "peridot/lib/fidl/json_xdr.h"

namespace modular {

namespace {

constexpr int kQueryActionMaxResults = 1;

}  // namespace

SuggestionEngineImpl::SuggestionEngineImpl(media::AudioServerPtr audio_server)
    : debug_(std::make_shared<SuggestionDebugImpl>()),
      next_processor_(debug_),
      query_processor_(std::move(audio_server), debug_),
      context_listener_binding_(this),
      auto_select_first_query_listener_(this),
      auto_select_first_query_listener_binding_(
          &auto_select_first_query_listener_) {}

SuggestionEngineImpl::~SuggestionEngineImpl() = default;

fxl::WeakPtr<SuggestionDebugImpl> SuggestionEngineImpl::debug() {
  return debug_->GetWeakPtr();
}

void SuggestionEngineImpl::AddNextProposal(ProposalPublisherImpl* source,
                                           Proposal proposal) {
  next_processor_.AddProposal(source->component_url(), std::move(proposal));
}

void SuggestionEngineImpl::RemoveNextProposal(const std::string& component_url,
                                              const std::string& proposal_id) {
  next_processor_.RemoveProposal(component_url, proposal_id);
}

void SuggestionEngineImpl::Connect(
    fidl::InterfaceRequest<SuggestionEngine> request) {
  bindings_.AddBinding(this, std::move(request));
}

void SuggestionEngineImpl::Connect(
    fidl::InterfaceRequest<SuggestionProvider> request) {
  suggestion_provider_bindings_.AddBinding(this, std::move(request));
}

void SuggestionEngineImpl::Connect(
    fidl::InterfaceRequest<SuggestionDebug> request) {
  debug_bindings_.AddBinding(debug_.get(), std::move(request));
}

// |SuggestionProvider|
void SuggestionEngineImpl::Query(fidl::InterfaceHandle<QueryListener> listener,
                                 UserInput input,
                                 int count) {
  query_processor_.ExecuteQuery(std::move(input), count, std::move(listener));
}

// |SuggestionProvider|
void SuggestionEngineImpl::SubscribeToInterruptions(
    fidl::InterfaceHandle<InterruptionListener> listener) {
  next_processor_.RegisterInterruptionListener(std::move(listener));
}

// |SuggestionProvider|
void SuggestionEngineImpl::SubscribeToNext(
    fidl::InterfaceHandle<NextListener> listener,
    int count) {
  next_processor_.RegisterListener(std::move(listener), count);
}

// |SuggestionProvider|
void SuggestionEngineImpl::RegisterFeedbackListener(
    fidl::InterfaceHandle<FeedbackListener> speech_listener) {
  query_processor_.RegisterFeedbackListener(std::move(speech_listener));
}

// |SuggestionProvider|
void SuggestionEngineImpl::NotifyInteraction(fidl::StringPtr suggestion_uuid,
                                             Interaction interaction) {
  // Find the suggestion
  bool suggestion_in_ask = false;
  RankedSuggestion* suggestion = next_processor_.GetSuggestion(suggestion_uuid);
  if (!suggestion) {
    suggestion = query_processor_.GetSuggestion(suggestion_uuid);
    suggestion_in_ask = true;
  }

  // If it exists (and it should), perform the action and clean up
  if (suggestion) {
    std::string log_detail = suggestion->prototype
                                 ? short_proposal_str(*suggestion->prototype)
                                 : "invalid";

    FXL_LOG(INFO) << (interaction.type == InteractionType::SELECTED
                          ? "Accepted"
                          : "Dismissed")
                  << " suggestion " << suggestion_uuid << " (" << log_detail
                  << ")";

    debug_->OnSuggestionSelected(suggestion->prototype);

    auto& proposal = suggestion->prototype->proposal;
    auto proposal_id = proposal.id;
    if (interaction.type == InteractionType::SELECTED) {
      PerformActions(std::move(proposal.on_selected),
                     std::move(proposal.listener), proposal.id,
                     suggestion->prototype->source_url, proposal.display.color);
    }

    if (suggestion_in_ask) {
      query_processor_.CleanUpPreviousQuery();
    } else {
      RemoveNextProposal(suggestion->prototype->source_url, proposal_id);
    }
  } else {
    FXL_LOG(WARNING) << "Requested suggestion prototype not found. UUID: "
                     << suggestion_uuid;
  }
}

// |SuggestionEngine|
void SuggestionEngineImpl::RegisterProposalPublisher(
    fidl::StringPtr url,
    fidl::InterfaceRequest<ProposalPublisher> publisher) {
  // Check to see if a ProposalPublisher has already been created for the
  // component with this url. If not, create one.
  std::unique_ptr<ProposalPublisherImpl>& source = proposal_publishers_[url];
  if (!source) {  // create if it didn't already exist
    source = std::make_unique<ProposalPublisherImpl>(this, url);
  }

  source->AddBinding(std::move(publisher));
}

// |SuggestionEngine|
void SuggestionEngineImpl::RegisterQueryHandler(
    fidl::StringPtr url,
    fidl::InterfaceHandle<QueryHandler> query_handler_handle) {
  query_processor_.RegisterQueryHandler(url, std::move(query_handler_handle));
}

// |SuggestionEngine|
void SuggestionEngineImpl::Initialize(
    fidl::InterfaceHandle<modular::StoryProvider> story_provider,
    fidl::InterfaceHandle<modular::FocusProvider> focus_provider,
    fidl::InterfaceHandle<ContextWriter> context_writer,
    fidl::InterfaceHandle<ContextReader> context_reader) {
  story_provider_.Bind(std::move(story_provider));
  focus_provider_ptr_.Bind(std::move(focus_provider));
  context_reader_.Bind(std::move(context_reader));
  query_processor_.Initialize(std::move(context_writer));
  RegisterRankingFeatures();
  timeline_stories_watcher_.reset(new TimelineStoriesWatcher(&story_provider_));
}

// end SuggestionEngine

void SuggestionEngineImpl::RegisterRankingFeatures() {
  // Create common ranking features
  ranking_features["proposal_hint_rf"] =
      std::make_shared<ProposalHintRankingFeature>();
  ranking_features["kronk_rf"] = std::make_shared<KronkRankingFeature>();
  ranking_features["mod_pairs_rf"] = std::make_shared<ModPairRankingFeature>();
  ranking_features["query_match_rf"] =
      std::make_shared<QueryMatchRankingFeature>();
  ranking_features["focused_story_rf"] =
      std::make_shared<FocusedStoryRankingFeature>();

  // Get context updates every time a story is focused to rerank suggestions
  // based on the story that is focused at the moment.
  ContextQuery query;
  for (auto const& it : ranking_features) {
    ContextSelectorPtr selector = it.second->CreateContextSelector();
    if (selector) {
      AddToContextQuery(&query, it.first, std::move(*selector));
    }
  }
  context_reader_->Subscribe(std::move(query),
                             context_listener_binding_.NewBinding());

  // TODO(jwnichols): Replace the code configuration of the ranking features
  // with a configuration file

  // Set up the next ranking features
  auto next_ranker = std::make_unique<LinearRanker>();
  next_ranker->AddRankingFeature(1.0, ranking_features["proposal_hint_rf"]);
  next_ranker->AddRankingFeature(-0.1, ranking_features["kronk_rf"]);
  next_ranker->AddRankingFeature(0, ranking_features["mod_pairs_rf"]);
  next_ranker->AddRankingFeature(1.0, ranking_features["focused_story_rf"]);
  next_processor_.SetRanker(std::move(next_ranker));

  // Set up the query ranking features
  auto query_ranker = std::make_unique<LinearRanker>();
  query_ranker->AddRankingFeature(1.0, ranking_features["proposal_hint_rf"]);
  query_ranker->AddRankingFeature(-0.1, ranking_features["kronk_rf"]);
  query_ranker->AddRankingFeature(0, ranking_features["mod_pairs_rf"]);
  query_ranker->AddRankingFeature(0, ranking_features["query_match_rf"]);
  query_processor_.SetRanker(std::move(query_ranker));
}

void SuggestionEngineImpl::PerformActions(
    fidl::VectorPtr<Action> actions,
    fidl::InterfaceHandle<ProposalListener> listener,
    const std::string& proposal_id,
    const std::string& source_url,
    uint32_t story_color) {
  // TODO(rosswang): If we're asked to add multiple modules, we probably
  // want to add them to the same story. We can't do that yet, but we need
  // to receive a StoryController anyway (not optional atm.).
  for (auto& action : *actions) {
    switch (action.Which()) {
      case Action::Tag::kCreateStory: {
        PerformCreateStoryAction(action, std::move(listener), proposal_id,
                                 story_color);
        break;
      }
      case Action::Tag::kFocusStory: {
        PerformFocusStoryAction(action);
        break;
      }
      case Action::Tag::kAddModule: {
        PerformAddModuleAction(action);
        break;
      }
      case Action::Tag::kQueryAction: {
        PerformQueryAction(action);
        break;
      }
      case Action::Tag::kCustomAction: {
        PerformCustomAction(&action, source_url, story_color);
        break;
      }
      default:
        FXL_LOG(WARNING) << "Unknown action tag " << (uint32_t)action.Which();
    }
  }

  if (listener) {
    auto proposal_listener = listener.Bind();
    proposal_listener->OnProposalAccepted(proposal_id, nullptr /* story_id */);
  }
}

void SuggestionEngineImpl::PerformCreateStoryAction(
    const Action& action,
    fidl::InterfaceHandle<ProposalListener> listener,
    const std::string& proposal_id,
    uint32_t story_color) {
  auto activity = debug_->GetIdleWaiter()->RegisterOngoingActivity();
  auto& create_story = action.create_story();

  if (!story_provider_) {
    FXL_LOG(WARNING) << "Unable to add module; no story provider";
    return;
  }

  Intent intent;
  if (create_story.intent) {
    intent = std::move(*create_story.intent);
  } else {
    intent.action.handler = create_story.module_id;
    if (create_story.initial_data) {
      IntentParameter root_parameter;
      root_parameter.name = nullptr;
      root_parameter.data.set_json(create_story.initial_data);
      intent.parameters.push_back(std::move(root_parameter));
    }
  }

  if (intent.action.handler) {
    FXL_LOG(INFO) << "Creating story with module " << intent.action.handler;
  } else {  // intent.action.name
    FXL_LOG(INFO) << "Creating story with action " << intent.action.name;
  }

  story_provider_->CreateStory(
      nullptr,
      fxl::MakeCopyable([this, listener = std::move(listener), proposal_id,
                         intent = std::move(intent),
                         activity](const fidl::StringPtr& story_id) mutable {
        modular::StoryControllerPtr story_controller;
        story_provider_->GetController(story_id, story_controller.NewRequest());
        // TODO(thatguy): We give the first module the name "root". We'd like to
        // move away from module names being assigned by the framework or other
        // components, and rather have clients always provide a module name.
        story_controller->AddModule(nullptr /* parent module path */,
                                    "root" /* module name */, std::move(intent),
                                    nullptr /* surface relation */);
        focus_provider_ptr_->Request(story_id);

        if (listener) {
          auto proposal_listener = listener.Bind();
          proposal_listener->OnProposalAccepted(proposal_id, story_id);
        }
      }));
}

void SuggestionEngineImpl::PerformFocusStoryAction(const Action& action) {
  const auto& focus_story = action.focus_story();
  FXL_LOG(INFO) << "Requesting focus for story_id " << focus_story.story_id;
  focus_provider_ptr_->Request(focus_story.story_id);
}

void SuggestionEngineImpl::PerformAddModuleAction(const Action& action) {
  if (story_provider_) {
    const auto& add_module = action.add_module();
    const auto& module_name = add_module.module_name;
    const auto& story_id = add_module.story_id;
    modular::StoryControllerPtr story_controller;
    story_provider_->GetController(story_id, story_controller.NewRequest());
    modular::Intent intent;
    fidl::Clone(add_module.intent, &intent);
    story_controller->AddModule(
        add_module.surface_parent_module_path.Clone(), module_name,
        std::move(intent), fidl::MakeOptional(add_module.surface_relation));
  } else {
    FXL_LOG(WARNING) << "Unable to add module; no story provider";
  }
}

void SuggestionEngineImpl::PerformCustomAction(Action* action,
                                               const std::string& source_url,
                                               uint32_t story_color) {
  auto activity = debug_->GetIdleWaiter()->RegisterOngoingActivity();
  auto custom_action = action->custom_action().Bind();
  custom_action->Execute(fxl::MakeCopyable(
      [this, activity, custom_action = std::move(custom_action), source_url,
       story_color](fidl::VectorPtr<ActionPtr> actions) {
        if (actions) {
          fidl::VectorPtr<Action> non_null_actions;
          for (auto& action : *actions) {
            if (action)
              non_null_actions.push_back(std::move(*action));
          }
          // Since there is no listener provided to PerformActions, it is fine
          // to use an empty proposal id here.
          PerformActions(std::move(non_null_actions), nullptr /* listener */,
                         "", source_url, story_color);
        }
      }));
}

void SuggestionEngineImpl::PerformQueryAction(const Action& action) {
  // TODO(miguelfrde): instead of keeping a AutoSelectFirstQueryListener as an
  // attribute. Create and move here through an internal structure.
  const auto& query_action = action.query_action();
  Query(auto_select_first_query_listener_binding_.NewBinding(),
        query_action.input, kQueryActionMaxResults);
}

void SuggestionEngineImpl::OnContextUpdate(ContextUpdate update) {
  for (auto const& it : ranking_features) {
    auto result = TakeContextValue(&update, it.first);
    if (result.first) {
      it.second->UpdateContext(result.second);
    }
  }
  next_processor_.UpdateRanking();
}

}  // namespace modular
