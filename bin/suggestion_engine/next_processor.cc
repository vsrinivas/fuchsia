// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/next_processor.h"

namespace maxwell {

NextProcessor::NextProcessor(SuggestionEngineImpl* engine)
    : engine_(engine), dirty_(false), processing_(false) {}

NextProcessor::~NextProcessor() = default;

void NextProcessor::RegisterListener(
    fidl::InterfaceHandle<NextListener> listener,
    const size_t max_results) {
  auto listenerPtr = listener.Bind();

  // Notify the listener of the current next suggestions
  NotifyOfResults(listenerPtr, max_results);

  // Save the listener
  listeners_.emplace_back(std::move(listenerPtr), max_results);

  // Register connection error handler on new listener to remove from list
  // if connection drops.  This code is mostly borrowed from InterfacePtrSet
  NextListenerPtr& nextPtr = listeners_.back().first;
  NextListener* pointer = nextPtr.get();
  nextPtr.set_error_handler([pointer, this]() {
    auto it = std::find_if(
        listeners_.begin(), listeners_.end(),
        [pointer](const auto& p) { return (p.first.get() == pointer); });
    assert(it != listeners_.end());
    listeners_.erase(it);
  });
}

void NextProcessor::AddProposal(const std::string& component_url,
                                ProposalPtr proposal) {
  NotifyOfProcessingChange(true);
  // The component_url and proposal ID form a unique identifier for a proposal.
  // If one already exists, remove it before adding the new one.
  RemoveProposal(component_url, proposal->id);

  auto suggestion = engine_->CreateSuggestionPrototype(component_url,
                                                       std::move(proposal));

  // TODO(jwnichols): Think more deeply about the intersection between the
  // interruption and next pipelines
  if (engine_->interruptions_processor_->ConsiderSuggestion(*suggestion)) {
    engine_->debug_.OnInterrupt(suggestion);
  }

  engine_->next_suggestions_->AddSuggestion(suggestion);
  dirty_ = true;
}

void NextProcessor::RemoveProposal(const std::string& component_url,
                                   const std::string& proposal_id) {
  NotifyOfProcessingChange(true);
  if (engine_->next_suggestions_->RemoveProposal(component_url, proposal_id)) {
    dirty_ = true;
  }
}

void NextProcessor::Validate() {
  if (dirty_) {
    engine_->next_suggestions_->Rank();
    NotifyAllOfResults();
    engine_->debug_.OnNextUpdate(engine_->next_suggestions_);
    NotifyOfProcessingChange(false);
    dirty_ = false;
  }
}

void NextProcessor::NotifyAllOfResults() {
  for (const auto& it : listeners_) {
    if (it.first)
      NotifyOfResults(it.first, it.second);
  }
}

void NextProcessor::NotifyOfProcessingChange(const bool processing) {
  if (processing_ != processing) {
    processing_ = processing;
    // Notify all listeners that the processing state has changed
    for (const auto& it : listeners_) {
      if (it.first)
        it.first->OnProcessingChange(processing_);
    }
  }
}

void NextProcessor::NotifyOfResults(const NextListenerPtr& listener,
                                    const size_t max_results) {
  const auto& suggestion_vector = engine_->next_suggestions_->Get();

  fidl::Array<SuggestionPtr> window;
  // Prefer to return an array of size 0 vs. null
  window.resize(0);
  for (size_t i = 0; i < max_results && i < suggestion_vector.size(); i++) {
    window.push_back(CreateSuggestion(*suggestion_vector[i]));
  }

  listener->OnNextResults(std::move(window));
}

}  // namespace maxwell
