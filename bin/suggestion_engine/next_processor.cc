// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/next_processor.h"

#include "peridot/bin/suggestion_engine/suggestion_engine_helper.h"

namespace modular {

NextProcessor::NextProcessor(std::shared_ptr<SuggestionDebugImpl> debug)
    : debug_(debug), processing_(false) {}

NextProcessor::~NextProcessor() = default;

void NextProcessor::RegisterListener(
    fidl::InterfaceHandle<fuchsia::modular::NextListener> listener,
    const size_t max_results) {
  auto listenerPtr = listener.Bind();

  // Notify the listener of the current next suggestions
  NotifyOfResults(listenerPtr, max_results);

  // Save the listener
  listeners_.emplace_back(std::move(listenerPtr), max_results);

  // Register connection error handler on new listener to remove from list
  // if connection drops.  This code is mostly borrowed from InterfacePtrSet
  fuchsia::modular::NextListenerPtr& nextPtr = listeners_.back().first;
  fuchsia::modular::NextListener* pointer = nextPtr.get();
  nextPtr.set_error_handler([pointer, this]() {
    auto it = std::find_if(
        listeners_.begin(), listeners_.end(),
        [pointer](const auto& p) { return (p.first.get() == pointer); });
    assert(it != listeners_.end());
    listeners_.erase(it);
  });
}

void NextProcessor::RegisterInterruptionListener(
    fidl::InterfaceHandle<fuchsia::modular::InterruptionListener> listener) {
  interruptions_processor_.RegisterListener(std::move(listener));
}

void NextProcessor::AddProposal(const std::string& component_url,
                                const std::string& story_id,
                                fuchsia::modular::Proposal proposal) {
  AddProposal(component_url, story_id, "" /* preloaded_story_id */,
              std::move(proposal));
}

void NextProcessor::AddProposal(
      const std::string& component_url,
      const std::string& story_id,
      const std::string& preloaded_story_id,
      fuchsia::modular::Proposal proposal) {
  NotifyOfProcessingChange(true);
  // The component_url and proposal ID form a unique identifier for a proposal.
  // If one already exists, remove it before adding the new one.
  RemoveProposal(component_url, proposal.id);

  auto prototype = CreateSuggestionPrototype(&prototypes_, component_url,
                                             story_id, preloaded_story_id,
                                             std::move(proposal));
  auto ranked_suggestion = RankedSuggestion::New(prototype);

  // TODO(miguelfrde): Make NextProcessor not depend on InterruptionsProcessor.
  if (interruptions_processor_.MaybeInterrupt(*ranked_suggestion)) {
    debug_->OnInterrupt(prototype);
  }

  suggestions_.AddSuggestion(std::move(ranked_suggestion));
  UpdateRanking();
}

void NextProcessor::RemoveProposal(const std::string& component_url,
                                   const std::string& proposal_id) {
  const auto key = std::make_pair(component_url, proposal_id);
  auto toRemove = prototypes_.find(key);
  if (toRemove != prototypes_.end()) {
    // can't erase right off the bat because the prototype must remain valid
    // until removed from the ranked list
    RemoveProposalFromList(component_url, proposal_id);
    prototypes_.erase(toRemove);
  }
}

void NextProcessor::RemoveProposalFromList(const std::string& component_url,
                                           const std::string& proposal_id) {
  NotifyOfProcessingChange(true);
  if (suggestions_.RemoveProposal(component_url, proposal_id)) {
    UpdateRanking();
  }
}

void NextProcessor::SetActiveFilters(
    std::vector<std::unique_ptr<SuggestionActiveFilter>>&& active_filters) {
  suggestions_.SetActiveFilters(std::move(active_filters));
}

void NextProcessor::SetPassiveFilters(
    std::vector<std::unique_ptr<SuggestionPassiveFilter>>&& passive_filters) {
  suggestions_.SetPassiveFilters(std::move(passive_filters));
}

void NextProcessor::SetRanker(std::unique_ptr<Ranker> ranker) {
  suggestions_.SetRanker(std::move(ranker));
}

void NextProcessor::SetInterruptionDecisionPolicy(
    std::unique_ptr<DecisionPolicy> decision_policy) {
  interruptions_processor_.SetDecisionPolicy(std::move(decision_policy));
}

RankedSuggestion* NextProcessor::GetSuggestion(
    const std::string& suggestion_id) const {
  return suggestions_.GetSuggestion(suggestion_id);
}

void NextProcessor::UpdateRanking() {
  suggestions_.Refresh();
  NotifyAllOfResults();
  debug_->OnNextUpdate(&suggestions_);
  NotifyOfProcessingChange(false);
}

void NextProcessor::NotifyAllOfResults() {
  for (const auto& it : listeners_) {
    if (it.first) {
      NotifyOfResults(it.first, it.second);
    }
  }
}

void NextProcessor::NotifyOfProcessingChange(const bool processing) {
  if (processing_ != processing) {
    processing_ = processing;
    // Notify all listeners that the processing state has changed
    for (const auto& it : listeners_) {
      if (it.first) {
        it.first->OnProcessingChange(processing_);
      }
    }
  }
}

void NextProcessor::NotifyOfResults(
    const fuchsia::modular::NextListenerPtr& listener,
    const size_t max_results) {
  const auto& suggestion_vector = suggestions_.Get();

  fidl::VectorPtr<fuchsia::modular::Suggestion> window;
  // Prefer to return an array of size 0 vs. null
  window.resize(0);
  for (size_t i = 0;
       window->size() < max_results && i < suggestion_vector.size(); i++) {
    if (!suggestion_vector[i]->hidden) {
      window.push_back(CreateSuggestion(*suggestion_vector[i]));
    }
  }

  listener->OnNextResults(std::move(window));
}

}  // namespace modular
