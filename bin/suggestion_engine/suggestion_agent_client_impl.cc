// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/suggestion_engine/suggestion_agent_client_impl.h"

namespace maxwell {
namespace suggestion {

void SuggestionAgentClientImpl::Propose(ProposalPtr proposal) {
  const size_t old_size = suggestions_.size();
  SuggestionRecord* suggestion_record = &suggestions_[proposal->id];

  if (suggestions_.size() > old_size)
    OnNewProposal(std::move(proposal), suggestion_record);
  else
    OnChangeProposal(std::move(proposal), suggestion_record);
}

void SuggestionAgentClientImpl::Remove(const fidl::String& proposal_id) {
  const auto it = suggestions_.find(proposal_id);

  if (it != suggestions_.end()) {
    const Suggestion& suggestion = it->second.suggestion;
    BroadcastRemoveSuggestion(suggestion);

    auto& ranked = suggestinator_->ranked_suggestions_;
    ranked.erase(std::find(ranked.begin(), ranked.end(), &suggestion));

    suggestinator_->suggestions_.erase(suggestion.uuid);
    suggestions_.erase(it);

    if (ShouldEraseSelf())
      EraseSelf();
  }
}

void SuggestionAgentClientImpl::GetAll(const GetAllCallback& callback) {
  // TODO
}

void SuggestionAgentClientImpl::BindingSet::OnConnectionError(
    fidl::Binding<SuggestionAgentClient>* binding) {
  maxwell::BindingSet<SuggestionAgentClient>::OnConnectionError(binding);

  if (impl_->ShouldEraseSelf())
    impl_->EraseSelf();
}

void SuggestionAgentClientImpl::BroadcastNewSuggestion(
    const Suggestion& suggestion) {
  for (const auto& subscriber : suggestinator_->next_subscribers_)
    subscriber->OnNewSuggestion(suggestion);
}

void SuggestionAgentClientImpl::BroadcastRemoveSuggestion(
    const Suggestion& suggestion) {
  for (const auto& subscriber : suggestinator_->next_subscribers_)
    subscriber->BeforeRemoveSuggestion(suggestion);
}

void SuggestionAgentClientImpl::OnNewProposal(
    ProposalPtr proposal,
    SuggestionRecord* suggestion_record) {
  Suggestion* suggestion = &suggestion_record->suggestion;
  ProposalToSuggestion(&proposal, suggestion);
  suggestion_record->source = this;
  suggestion_record->proposal = std::move(proposal);

  // TODO(rosswang): sort
  suggestinator_->ranked_suggestions_.emplace_back(suggestion);

  suggestinator_->suggestions_[suggestion->uuid] = suggestion_record;

  BroadcastNewSuggestion(*suggestion);
}

void SuggestionAgentClientImpl::OnChangeProposal(
    ProposalPtr proposal,
    SuggestionRecord* suggestion_record) {
  Suggestion& suggestion = suggestion_record->suggestion;
  BroadcastRemoveSuggestion(suggestion);

  // TODO(rosswang): re-rank if necessary
  suggestion.display = std::move(proposal->display);

  suggestion_record->proposal = std::move(proposal);

  BroadcastNewSuggestion(suggestion);
}

void SuggestionAgentClientImpl::EraseSelf() {
  suggestinator_->sources_.erase(component_url_);
}

}  // namespace suggestion
}  // namespace maxwell
