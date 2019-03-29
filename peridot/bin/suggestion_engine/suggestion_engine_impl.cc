// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/suggestion_engine_impl.h"

#include <string>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/context/cpp/context_helper.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fsl/vmo/strings.h>
#include <src/lib/fxl/time/time_delta.h>
#include <src/lib/fxl/time/time_point.h>
#include "lib/fidl/cpp/clone.h"
#include "src/lib/uuid/uuid.h"

#include "peridot/bin/suggestion_engine/decision_policies/rank_over_threshold_decision_policy.h"
#include "peridot/bin/suggestion_engine/filters/conjugate_ranked_passive_filter.h"
#include "peridot/bin/suggestion_engine/filters/ranked_active_filter.h"
#include "peridot/bin/suggestion_engine/filters/ranked_passive_filter.h"
#include "peridot/bin/suggestion_engine/rankers/linear_ranker.h"
#include "peridot/bin/suggestion_engine/ranking_features/affinity_ranking_feature.h"
#include "peridot/bin/suggestion_engine/ranking_features/annoyance_ranking_feature.h"
#include "peridot/bin/suggestion_engine/ranking_features/dead_story_ranking_feature.h"
#include "peridot/bin/suggestion_engine/ranking_features/interrupting_ranking_feature.h"
#include "peridot/bin/suggestion_engine/ranking_features/kronk_ranking_feature.h"
#include "peridot/bin/suggestion_engine/ranking_features/mod_pair_ranking_feature.h"
#include "peridot/bin/suggestion_engine/ranking_features/proposal_hint_ranking_feature.h"
#include "peridot/bin/suggestion_engine/ranking_features/query_match_ranking_feature.h"
#include "peridot/bin/suggestion_engine/ranking_features/ranking_feature.h"
#include "peridot/lib/fidl/json_xdr.h"

namespace modular {

SuggestionEngineImpl::SuggestionEngineImpl(
    fuchsia::modular::ContextReaderPtr context_reader,
    fuchsia::modular::PuppetMasterPtr puppet_master)
    : debug_(std::make_shared<SuggestionDebugImpl>()),
      next_processor_(debug_),
      query_processor_(debug_),
      context_reader_(std::move(context_reader)),
      puppet_master_(std::move(puppet_master)),
      context_listener_binding_(this) {
  RegisterRankingFeatures();
}

SuggestionEngineImpl::~SuggestionEngineImpl() = default;

fxl::WeakPtr<SuggestionDebugImpl> SuggestionEngineImpl::debug() {
  return debug_->GetWeakPtr();
}

void SuggestionEngineImpl::AddNextProposal(
    ProposalPublisherImpl* source, fuchsia::modular::Proposal proposal) {
  if (proposal.wants_rich_suggestion) {
    if (ComponentCanUseRichSuggestions(source->component_url())) {
      AddProposalWithRichSuggestion(source, std::move(proposal));
      return;
    }
    FXL_LOG(INFO) << "Attempt to add rich suggestion for unallowed component "
                  << source->component_url();
  }
  next_processor_.AddProposal(source->component_url(), std::move(proposal));
}

void SuggestionEngineImpl::ProposeNavigation(
    const fuchsia::modular::NavigationAction navigation) {
  navigation_processor_.Navigate(navigation);
}

void SuggestionEngineImpl::AddProposalWithRichSuggestion(
    ProposalPublisherImpl* source, fuchsia::modular::Proposal proposal) {
  if (!proposal.story_name) {
    // Puppet master will generate a story name on execution of the
    // proposal actions.
    proposal.story_name = "";
  }

  SuggestionPrototype* suggestion =
      next_processor_.GetSuggestion(source->component_url(), proposal.id);

  // We keep track of the previous story since a new one will be created for a
  // existing proposal.
  // TODO(miguelfrde): this logic should probably belong in NextProcessor. We
  // should also allow clients to reuse the story_name and mod_name to update
  // the mod in the suggestion directly rather than creating a new one, however
  // this is not working yet.
  std::string existing_story;
  if (suggestion && !suggestion->preloaded_story_id.empty()) {
    existing_story = suggestion->preloaded_story_id;
  }

  fuchsia::modular::StoryPuppetMasterPtr story_puppet_master;
  puppet_master_->ControlStory(proposal.story_name,
                               story_puppet_master.NewRequest());
  fuchsia::modular::StoryOptions story_options;
  story_options.kind_of_proto_story = true;
  story_puppet_master->SetCreateOptions(std::move(story_options));

  auto fut = modular::Future<fuchsia::modular::ExecuteResult>::Create(
      "SuggestionEngine::AddProposalWithRichSuggestion.fut");
  story_puppet_master->Enqueue(std::move(proposal.on_selected));
  story_puppet_master->Execute(fut->Completer());
  fut->Then([this, fut, source_url = source->component_url(),
             proposal = std::move(proposal),
             sp = std::move(story_puppet_master),
             existing_story](fuchsia::modular::ExecuteResult result) mutable {
    if (result.status != fuchsia::modular::ExecuteStatus::OK) {
      FXL_LOG(WARNING) << "Preloading of rich suggestion actions resulted "
                       << "non successful status=" << (uint32_t)result.status
                       << " message=" << result.error_message;
    }
    if (proposal.story_name->empty()) {
      proposal.story_name = result.story_id;
    }

    if (existing_story.empty()) {
      next_processor_.AddProposal(source_url, result.story_id,
                                  std::move(proposal));
    }
  });
}

void SuggestionEngineImpl::RemoveNextProposal(const std::string& component_url,
                                              const std::string& proposal_id) {
  SuggestionPrototype* suggestion =
      next_processor_.GetSuggestion(component_url, proposal_id);
  if (suggestion && !suggestion->preloaded_story_id.empty()) {
    auto story_name = suggestion->proposal.story_name;
    puppet_master_->DeleteStory(
        story_name, [this, story_name, component_url, proposal_id] {
          next_processor_.RemoveProposal(component_url, proposal_id);
        });
  } else {
    next_processor_.RemoveProposal(component_url, proposal_id);
  }
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
void SuggestionEngineImpl::SubscribeToNavigation(
    fidl::InterfaceHandle<fuchsia::modular::NavigationListener> listener) {
  navigation_processor_.RegisterListener(std::move(listener));
}

// |fuchsia::modular::SuggestionProvider|
void SuggestionEngineImpl::SubscribeToNext(
    fidl::InterfaceHandle<fuchsia::modular::NextListener> listener, int count) {
  next_processor_.RegisterListener(std::move(listener), count);
}

// |fuchsia::modular::SuggestionProvider|
void SuggestionEngineImpl::NotifyInteraction(
    std::string suggestion_uuid, fuchsia::modular::Interaction interaction) {
  // Find the suggestion
  bool suggestion_in_ask = false;
  RankedSuggestion* suggestion = next_processor_.GetSuggestion(suggestion_uuid);
  if (!suggestion) {
    suggestion = query_processor_.GetSuggestion(suggestion_uuid);
    suggestion_in_ask = true;
  }

  if (!suggestion) {
    FXL_LOG(WARNING) << "Requested suggestion in notify interaction not found. "
                     << "UUID: " << suggestion_uuid;
    return;
  }

  // If it exists (and it should), perform the action and clean up
  auto component_url = suggestion->prototype->source_url;
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
  auto preloaded_story_id = suggestion->prototype->preloaded_story_id;
  suggestion->interrupting = false;

  switch (interaction.type) {
    case fuchsia::modular::InteractionType::SELECTED: {
      HandleSelectedInteraction(
          component_url, preloaded_story_id, proposal,
          std::move(suggestion->prototype->bound_listener), suggestion_in_ask);
      break;
    }
    case fuchsia::modular::InteractionType::DISMISSED: {
      if (suggestion_in_ask) {
        query_processor_.CleanUpPreviousQuery();
      } else {
        RemoveNextProposal(component_url, proposal_id);
      }
      break;
    }
    case fuchsia::modular::InteractionType::EXPIRED:
    case fuchsia::modular::InteractionType::SNOOZED: {
      // No need to remove since it was either expired by a timeout in
      // session shell or snoozed by the user, however we should still refresh
      // the next processor (if not in ask) given that `interrupting=false` set
      // above.
      if (!suggestion_in_ask) {
        next_processor_.UpdateRanking();
      }
      break;
    }
  }
}

// |fuchsia::modular::SuggestionEngine|
void SuggestionEngineImpl::RegisterProposalPublisher(
    std::string url,
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
    std::string url, fidl::InterfaceHandle<fuchsia::modular::QueryHandler>
                         query_handler_handle) {
  query_processor_.RegisterQueryHandler(url, std::move(query_handler_handle));
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
  ranking_features["affinity_rf"] = std::make_shared<AffinityRankingFeature>();
  ranking_features["annoyance_rf"] =
      std::make_shared<AnnoyanceRankingFeature>();
  ranking_features["dead_story_rf"] =
      std::make_shared<DeadStoryRankingFeature>();
  ranking_features["is_interrupting_rf"] =
      std::make_shared<InterruptingRankingFeature>();

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
  next_ranker->AddRankingFeature(1.0, ranking_features["affinity_rf"]);
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
      ranking_features["affinity_rf"]));
  passive_filters.push_back(std::make_unique<RankedPassiveFilter>(
      ranking_features["is_interrupting_rf"]));
  next_processor_.SetPassiveFilters(std::move(passive_filters));
}

void SuggestionEngineImpl::OnContextUpdate(
    fuchsia::modular::ContextUpdate update) {
  for (auto& entry : update.values) {
    for (const auto& rf_it : ranking_features) {
      if (entry.key == rf_it.first) {  // Update key == rf key
        rf_it.second->UpdateContext(entry.value);
      }
    }
  }
  next_processor_.UpdateRanking();
}

bool SuggestionEngineImpl::ComponentCanUseRichSuggestions(
    const std::string& component_url) {
  // Only kronk is allowed to preload stories in suggestions to make
  // rich suggestions.
  // Proposinator is used for testing.
  return component_url.find("kronk") != std::string::npos ||
         component_url.find("krohnkite") != std::string::npos ||
         component_url.find("Proposinator") != std::string::npos;
}

void SuggestionEngineImpl::HandleSelectedInteraction(
    const std::string& component_url, const std::string& preloaded_story_id,
    fuchsia::modular::Proposal& proposal,
    fuchsia::modular::ProposalListenerPtr listener, bool suggestion_in_ask) {
  // Rich suggestions are only in Next, so we don't check suggestion_in_ask.
  if (!preloaded_story_id.empty()) {
    if (listener) {
      listener->OnProposalAccepted(proposal.id, preloaded_story_id);
    }
    // TODO(miguelfrde): eventually we should promote stories here. For now rich
    // suggestions aren't removed or promoted.
    return;
  }

  if (!proposal.story_name) {
    // Puppet master will generate a story name.
    proposal.story_name = "";
  }
  fuchsia::modular::StoryPuppetMasterPtr story_puppet_master;
  puppet_master_->ControlStory(proposal.story_name,
                               story_puppet_master.NewRequest());
  auto fut = modular::Future<fuchsia::modular::ExecuteResult>::Create(
      "SuggestionEngine::HandleSelectedInteraction.fut");
  // TODO(miguelfred): break up |commands| if it is too large of a list for one
  // FIDL message.
  story_puppet_master->Enqueue(std::move(proposal.on_selected));
  story_puppet_master->Execute(fut->Completer());
  fut->Then([this, proposal_id = proposal.id, suggestion_in_ask, component_url,
             listener = std::move(listener),
             sp = std::move(story_puppet_master),
             fut](fuchsia::modular::ExecuteResult result) mutable {
    // TODO(miguelfrde): check status.
    if (listener) {
      listener->OnProposalAccepted(proposal_id, result.story_id);
    }
    if (suggestion_in_ask) {
      query_processor_.CleanUpPreviousQuery();
    } else {
      next_processor_.RemoveProposal(component_url, proposal_id);
    }
  });
}

}  // namespace modular
