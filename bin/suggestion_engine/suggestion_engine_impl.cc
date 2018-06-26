// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/suggestion_engine_impl.h"

#include <string>

#include <fuchsia/modular/cpp/fidl.h>

#include "lib/context/cpp/context_helper.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/time/time_delta.h"
#include "lib/fxl/time/time_point.h"
#include "peridot/bin/suggestion_engine/auto_select_first_query_listener.h"
#include "peridot/bin/suggestion_engine/decision_policies/rank_over_threshold_decision_policy.h"
#include "peridot/bin/suggestion_engine/filters/conjugate_ranked_passive_filter.h"
#include "peridot/bin/suggestion_engine/filters/ranked_active_filter.h"
#include "peridot/bin/suggestion_engine/rankers/linear_ranker.h"
#include "peridot/bin/suggestion_engine/ranking_features/annoyance_ranking_feature.h"
#include "peridot/bin/suggestion_engine/ranking_features/dead_story_ranking_feature.h"
#include "peridot/bin/suggestion_engine/ranking_features/focused_story_ranking_feature.h"
#include "peridot/bin/suggestion_engine/ranking_features/kronk_ranking_feature.h"
#include "peridot/bin/suggestion_engine/ranking_features/mod_pair_ranking_feature.h"
#include "peridot/bin/suggestion_engine/ranking_features/proposal_hint_ranking_feature.h"
#include "peridot/bin/suggestion_engine/ranking_features/query_match_ranking_feature.h"
#include "peridot/bin/suggestion_engine/ranking_features/ranking_feature.h"
#include "peridot/lib/fidl/json_xdr.h"

namespace modular {

std::string StoryNameKey(const std::string& source_url,
                         const std::string& story_name) {
  return source_url + story_name;
}

namespace {

constexpr int kQueryActionMaxResults = 1;

}  // namespace

SuggestionEngineImpl::SuggestionEngineImpl(fuchsia::media::AudioPtr audio)
    : debug_(std::make_shared<SuggestionDebugImpl>()),
      next_processor_(debug_),
      query_processor_(std::move(audio), debug_),
      context_listener_binding_(this),
      auto_select_first_query_listener_(this),
      auto_select_first_query_listener_binding_(
          &auto_select_first_query_listener_) {}

SuggestionEngineImpl::~SuggestionEngineImpl() = default;

fxl::WeakPtr<SuggestionDebugImpl> SuggestionEngineImpl::debug() {
  return debug_->GetWeakPtr();
}

void SuggestionEngineImpl::AddNextProposal(
    ProposalPublisherImpl* source, fuchsia::modular::Proposal proposal) {
  auto story_id = StoryIdFromName(source->component_url(), proposal.story_name);
  next_processor_.AddProposal(source->component_url(), story_id,
                              std::move(proposal));
}

void SuggestionEngineImpl::RemoveNextProposal(const std::string& component_url,
                                              const std::string& proposal_id) {
  next_processor_.RemoveProposal(component_url, proposal_id);
}

void SuggestionEngineImpl::Connect(
    fidl::InterfaceRequest<fuchsia::modular::SuggestionEngine> request) {
  bindings_.AddBinding(this, std::move(request));
}

void SuggestionEngineImpl::Connect(
    fidl::InterfaceRequest<fuchsia::modular::SuggestionProvider> request) {
  suggestion_provider_bindings_.AddBinding(this, std::move(request));
}

void SuggestionEngineImpl::Connect(
    fidl::InterfaceRequest<fuchsia::modular::SuggestionDebug> request) {
  debug_bindings_.AddBinding(debug_.get(), std::move(request));
}

// |fuchsia::modular::SuggestionProvider|
void SuggestionEngineImpl::Query(
    fidl::InterfaceHandle<fuchsia::modular::QueryListener> listener,
    fuchsia::modular::UserInput input, int count) {
  query_processor_.ExecuteQuery(std::move(input), count, std::move(listener));
}

// |fuchsia::modular::SuggestionProvider|
void SuggestionEngineImpl::SubscribeToInterruptions(
    fidl::InterfaceHandle<fuchsia::modular::InterruptionListener> listener) {
  next_processor_.RegisterInterruptionListener(std::move(listener));
}

// |fuchsia::modular::SuggestionProvider|
void SuggestionEngineImpl::SubscribeToNext(
    fidl::InterfaceHandle<fuchsia::modular::NextListener> listener, int count) {
  next_processor_.RegisterListener(std::move(listener), count);
}

// |fuchsia::modular::SuggestionProvider|
void SuggestionEngineImpl::RegisterFeedbackListener(
    fidl::InterfaceHandle<fuchsia::modular::FeedbackListener> speech_listener) {
  query_processor_.RegisterFeedbackListener(std::move(speech_listener));
}

// |fuchsia::modular::SuggestionProvider|
void SuggestionEngineImpl::NotifyInteraction(
    fidl::StringPtr suggestion_uuid,
    fuchsia::modular::Interaction interaction) {
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

    FXL_LOG(INFO) << (interaction.type ==
                              fuchsia::modular::InteractionType::SELECTED
                          ? "Accepted"
                          : "Dismissed")
                  << " suggestion " << suggestion_uuid << " (" << log_detail
                  << ")";

    debug_->OnSuggestionSelected(suggestion->prototype);

    auto& proposal = suggestion->prototype->proposal;
    auto proposal_id = proposal.id;
    if (interaction.type == fuchsia::modular::InteractionType::SELECTED) {
      PerformActions(std::move(proposal.on_selected),
                     std::move(proposal.listener), proposal.id,
                     proposal.story_name, suggestion->prototype->source_url,
                     std::move(proposal.display));
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

// |fuchsia::modular::SuggestionEngine|
void SuggestionEngineImpl::RegisterProposalPublisher(
    fidl::StringPtr url,
    fidl::InterfaceRequest<fuchsia::modular::ProposalPublisher> publisher) {
  // Check to see if a fuchsia::modular::ProposalPublisher has already been
  // created for the component with this url. If not, create one.
  std::unique_ptr<ProposalPublisherImpl>& source = proposal_publishers_[url];
  if (!source) {  // create if it didn't already exist
    source = std::make_unique<ProposalPublisherImpl>(this, url);
  }

  source->AddBinding(std::move(publisher));
}

// |fuchsia::modular::SuggestionEngine|
void SuggestionEngineImpl::RegisterQueryHandler(
    fidl::StringPtr url, fidl::InterfaceHandle<fuchsia::modular::QueryHandler>
                             query_handler_handle) {
  query_processor_.RegisterQueryHandler(url, std::move(query_handler_handle));
}

// |fuchsia::modular::SuggestionEngine|
void SuggestionEngineImpl::Initialize(
    fidl::InterfaceHandle<fuchsia::modular::StoryProvider> story_provider,
    fidl::InterfaceHandle<fuchsia::modular::FocusProvider> focus_provider,
    fidl::InterfaceHandle<fuchsia::modular::ContextWriter> context_writer,
    fidl::InterfaceHandle<fuchsia::modular::ContextReader> context_reader) {
  story_provider_.Bind(std::move(story_provider));
  focus_provider_ptr_.Bind(std::move(focus_provider));
  context_reader_.Bind(std::move(context_reader));
  query_processor_.Initialize(std::move(context_writer));
  RegisterRankingFeatures();
}

// end fuchsia::modular::SuggestionEngine

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
  ranking_features["annoyance_rf"] =
      std::make_shared<AnnoyanceRankingFeature>();
  ranking_features["dead_story_rf"] =
      std::make_shared<DeadStoryRankingFeature>();

  // Get context updates every time a story is focused to rerank suggestions
  // based on the story that is focused at the moment.
  fuchsia::modular::ContextQuery query;
  for (auto const& it : ranking_features) {
    fuchsia::modular::ContextSelectorPtr selector =
        it.second->CreateContextSelector();
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

  // Set up the interrupt ranking features
  auto interrupt_ranker = std::make_unique<LinearRanker>();
  interrupt_ranker->AddRankingFeature(1.0, ranking_features["annoyance_rf"]);
  auto decision_policy = std::make_unique<RankOverThresholdDecisionPolicy>(
      std::move(interrupt_ranker));
  next_processor_.SetInterruptionDecisionPolicy(std::move(decision_policy));

  // Set up passive filters
  std::vector<std::unique_ptr<SuggestionPassiveFilter>> passive_filters;
  passive_filters.push_back(std::make_unique<ConjugateRankedPassiveFilter>(
      ranking_features["focused_story_rf"]));
  next_processor_.SetPassiveFilters(std::move(passive_filters));
}

void SuggestionEngineImpl::PerformActions(
    fidl::VectorPtr<fuchsia::modular::Action> actions,
    fidl::InterfaceHandle<fuchsia::modular::ProposalListener> listener,
    const std::string& proposal_id, const std::string& story_name,
    const std::string& source_url,
    fuchsia::modular::SuggestionDisplay suggestion_display) {
  if (story_name.empty()) {
    ExecuteActions(std::move(actions), std::move(listener), proposal_id,
                   std::move(suggestion_display), "" /* override_story_id */);
    return;
  }
  const std::string key = StoryNameKey(source_url, story_name);
  auto it = story_name_mapping_.find(key);
  if (it == story_name_mapping_.end()) {
    story_provider_->CreateStory(
        nullptr /* module_url */,
        fxl::MakeCopyable(
            [this, actions = std::move(actions), listener = std::move(listener),
             proposal_id, suggestion_display = std::move(suggestion_display),
             story_name, source_url](const fidl::StringPtr& story_id) mutable {
              story_name_mapping_[StoryNameKey(source_url, story_name)] =
                  story_id;
              // TODO(miguelfrde): better expect clients to send focus action?
              focus_provider_ptr_->Request(story_id);
              ExecuteActions(std::move(actions), std::move(listener),
                             proposal_id, std::move(suggestion_display),
                             story_id);
            }));
  } else {
    ExecuteActions(std::move(actions), std::move(listener), proposal_id,
                   std::move(suggestion_display), it->second);
  }
}

void SuggestionEngineImpl::ExecuteActions(
    fidl::VectorPtr<fuchsia::modular::Action> actions,
    fidl::InterfaceHandle<fuchsia::modular::ProposalListener> listener,
    const std::string& proposal_id,
    fuchsia::modular::SuggestionDisplay suggestion_display,
    const std::string& override_story_id) {
  for (auto& action : *actions) {
    switch (action.Which()) {
      case fuchsia::modular::Action::Tag::kCreateStory: {
        // TODO(miguelfrde): deprecated, remove.
        fuchsia::modular::SuggestionDisplay cloned_display;
        suggestion_display.Clone(&cloned_display);
        PerformCreateStoryAction(action, std::move(listener), proposal_id,
                                 std::move(cloned_display));
        break;
      }
      case fuchsia::modular::Action::Tag::kFocusStory: {
        PerformFocusStoryAction(action, override_story_id);
        break;
      }
      case fuchsia::modular::Action::Tag::kAddModule: {
        PerformAddModuleAction(action, override_story_id);
        break;
      }
      case fuchsia::modular::Action::Tag::kQueryAction: {
        PerformQueryAction(action);
        break;
      }
      case fuchsia::modular::Action::Tag::kSetLinkValueAction: {
        PerformSetLinkValueAction(action, override_story_id);
        break;
      }
      case fuchsia::modular::Action::Tag::kUpdateModule: {
        PerformUpdateModuleAction(&action, override_story_id);
        break;
      }
      case fuchsia::modular::Action::Tag::kCustomAction: {
        PerformCustomAction(&action);
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
    const fuchsia::modular::Action& action,
    fidl::InterfaceHandle<fuchsia::modular::ProposalListener> listener,
    const std::string& proposal_id,
    fuchsia::modular::SuggestionDisplay suggestion_display) {
  auto activity = debug_->GetIdleWaiter()->RegisterOngoingActivity();
  auto& create_story = action.create_story();

  if (!story_provider_) {
    FXL_LOG(WARNING) << "Unable to add module; no story provider";
    return;
  }

  fuchsia::modular::Intent intent;
  create_story.intent.Clone(&intent);

  if (intent.action.handler) {
    FXL_LOG(INFO) << "Creating story with module " << intent.action.handler;
  } else {  // intent.action.name
    FXL_LOG(INFO) << "Creating story with action " << intent.action.name;
  }

  // TODO(MI4-997): Use a separate enum for internal ranking vs. what is exposed
  // to the user shell for display purposes.
  fuchsia::modular::StoryInfoExtraEntry extra_entry;
  extra_entry.key = "annoyance_type";
  switch (suggestion_display.annoyance) {
    case fuchsia::modular::AnnoyanceType::NONE:
      extra_entry.value = "none";
      break;
    case fuchsia::modular::AnnoyanceType::PEEK:
      extra_entry.value = "peek";
      break;
    case fuchsia::modular::AnnoyanceType::INTERRUPT:
      extra_entry.value = "interrupt";
      break;
  }
  fidl::VectorPtr<fuchsia::modular::StoryInfoExtraEntry> extra_info;
  extra_info.push_back(std::move(extra_entry));

  story_provider_->CreateStoryWithInfo(
      nullptr /* module_url */, std::move(extra_info), nullptr /* root_json */,
      fxl::MakeCopyable([this, listener = std::move(listener), proposal_id,
                         intent = std::move(intent),
                         activity](const fidl::StringPtr& story_id) mutable {
        fuchsia::modular::StoryControllerPtr story_controller;
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

void SuggestionEngineImpl::PerformFocusStoryAction(
    const fuchsia::modular::Action& action,
    const std::string& override_story_id) {
  const auto& focus_story = action.focus_story();
  std::string story_id = focus_story.story_id;
  if (!override_story_id.empty()) {
    story_id = override_story_id;
    if (override_story_id != focus_story.story_id) {
      FXL_LOG(WARNING)
          << "story_id provided on fuchsia::modular::Proposal ("
          << override_story_id
          << ") does not match that on fuchsia::modular::FocusStory action ("
          << focus_story.story_id << "). Using " << override_story_id << ".";
    }
  }
  FXL_LOG(INFO) << "Requesting focus for story_id " << story_id;
  focus_provider_ptr_->Request(story_id);
}

void SuggestionEngineImpl::PerformAddModuleAction(
    const fuchsia::modular::Action& action,
    const std::string& override_story_id) {
  if (!story_provider_) {
    FXL_LOG(WARNING) << "Unable to add module; no story provider";
    return;
  }
  const auto& add_module = action.add_module();
  const auto& module_name = add_module.module_name;
  std::string story_id = add_module.story_id;

  if (!override_story_id.empty()) {
    story_id = override_story_id;
    if (override_story_id != add_module.story_id) {
      FXL_LOG(WARNING)
          << "story_id provided on fuchsia::modular::Proposal ("
          << override_story_id
          << ") does not match that on fuchsia::modular::AddModule action ("
          << add_module.story_id << "). Using " << override_story_id << ".";
    }
  }
  fuchsia::modular::StoryControllerPtr story_controller;
  story_provider_->GetController(story_id, story_controller.NewRequest());
  fuchsia::modular::Intent intent;
  fidl::Clone(add_module.intent, &intent);
  story_controller->AddModule(add_module.surface_parent_module_path.Clone(),
                              module_name, std::move(intent),
                              fidl::MakeOptional(add_module.surface_relation));
}

void SuggestionEngineImpl::PerformUpdateModuleAction(
    fuchsia::modular::Action* const action, const std::string& story_id) {
  if (!story_provider_) {
    FXL_LOG(WARNING) << "Unable to set entity; no story provider";
    return;
  }

  fuchsia::modular::StoryControllerPtr story_controller;
  story_provider_->GetController(story_id, story_controller.NewRequest());
  story_controller->GetModules(fxl::MakeCopyable(
      [this, story_controller = std::move(story_controller),
       module_name = std::move(action->update_module().module_name),
       parameters = std::move(action->update_module().parameters)](
          fidl::VectorPtr<fuchsia::modular::ModuleData> module_datas) {
        for (const auto& module_data : *module_datas) {
          if (module_data.module_path != module_name) {
            continue;
          }
          for (auto& parameter : *parameters) {
            for (auto& entry : *module_data.parameter_map.entries) {
              if (entry.name != parameter.name) {
                continue;
              }
              fuchsia::modular::LinkPtr link;
              story_controller->GetLink(entry.link_path.module_path.Clone(),
                                        std::move(entry.link_path.link_name),
                                        link.NewRequest());
              switch (parameter.data.Which()) {
                case fuchsia::modular::IntentParameterData::Tag::
                    kEntityReference: {
                  link->SetEntity(parameter.data.entity_reference());
                  break;
                }
                case fuchsia::modular::IntentParameterData::Tag::kJson: {
                  link->Set(nullptr, parameter.data.json());
                  break;
                }
                case fuchsia::modular::IntentParameterData::Tag::kEntityType:
                case fuchsia::modular::IntentParameterData::Tag::kLinkName:
                case fuchsia::modular::IntentParameterData::Tag::kLinkPath:
                case fuchsia::modular::IntentParameterData::Tag::Invalid: {
                  FXL_LOG(WARNING) << "fuchsia::modular::UpdateModule action "
                                      "with unsupported "
                                   << "parameter data tag #"
                                   << (uint32_t)parameter.data.Which();
                  break;
                }
              }
            }
          }
        }
      }));
}

void SuggestionEngineImpl::PerformCustomAction(
    fuchsia::modular::Action* action) {
  action->custom_action().Bind()->Execute();
}

void SuggestionEngineImpl::PerformSetLinkValueAction(
    const fuchsia::modular::Action& action, const std::string& story_id) {
  if (!story_provider_) {
    FXL_LOG(WARNING) << "Unable to set entity; no story provider";
    return;
  }

  fuchsia::modular::StoryControllerPtr story_controller;
  story_provider_->GetController(story_id, story_controller.NewRequest());

  const auto& set_link_value = action.set_link_value_action();
  const auto& link_path = set_link_value.link_path;
  fuchsia::modular::LinkPtr link;
  story_controller->GetLink(link_path.module_path.Clone(), link_path.link_name,
                            link.NewRequest());
  link->Set(nullptr, set_link_value.value);
}

void SuggestionEngineImpl::PerformQueryAction(
    const fuchsia::modular::Action& action) {
  // TODO(miguelfrde): instead of keeping a AutoSelectFirstQueryListener as an
  // attribute. Create and move here through an internal structure.
  const auto& query_action = action.query_action();
  Query(auto_select_first_query_listener_binding_.NewBinding(),
        query_action.input, kQueryActionMaxResults);
}

void SuggestionEngineImpl::OnContextUpdate(
    fuchsia::modular::ContextUpdate update) {
  for (auto& entry : update.values.take()) {
    for (const auto& rf_it : ranking_features) {
      if (entry.key == rf_it.first) {  // Update key == rf key
        rf_it.second->UpdateContext(entry.value);
      }
    }
  }
  next_processor_.UpdateRanking();
}

std::string SuggestionEngineImpl::StoryIdFromName(
    const std::string& source_url, const std::string& story_name) {
  auto it = story_name_mapping_.find(StoryNameKey(source_url, story_name));
  if (it != story_name_mapping_.end()) {
    return it->second;
  }
  return "";
}

}  // namespace modular
