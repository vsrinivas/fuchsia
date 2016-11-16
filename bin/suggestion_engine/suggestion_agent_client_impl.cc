// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/suggestion_engine/suggestion_agent_client_impl.h"

namespace maxwell {
namespace suggestion {

void SuggestionAgentClientImpl::Propose(ProposalPtr proposal) {
  const size_t old_size = suggestions_.size();
  Suggestion* suggestion = &suggestions_[proposal->id];

  if (suggestions_.size() > old_size)
    OnNewProposal(*proposal, suggestion);
  else
    OnChangeProposal(*proposal, suggestion);
}

void SuggestionAgentClientImpl::Remove(const fidl::String& proposal_id) {
  const auto it = suggestions_.find(proposal_id);

  if (it != suggestions_.end()) {
    const Suggestion& suggestion = it->second;
    BroadcastRemoveSuggestion(suggestion);
    auto& ranked = suggestinator_->ranked_suggestions_;
    ranked.erase(std::find(ranked.begin(), ranked.end(), &suggestion));
    suggestions_.erase(it);

    if (suggestions_.empty() && bindings_.empty())
      EraseSelf();
  }
}

void SuggestionAgentClientImpl::GetAll(const GetAllCallback& callback) {
  // TODO
}

void SuggestionAgentClientImpl::BindingSet::OnConnectionError(
    fidl::Binding<SuggestionAgentClient>* binding) {
  maxwell::BindingSet<SuggestionAgentClient>::OnConnectionError(binding);

  if (empty() && impl_->suggestions_.empty())
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

void SuggestionAgentClientImpl::OnNewProposal(const Proposal& proposal,
                                              Suggestion* suggestion) {
  ProposalToSuggestion(proposal, suggestion);

  // TODO(rosswang): sort
  suggestinator_->ranked_suggestions_.emplace_back(suggestion);

  BroadcastNewSuggestion(*suggestion);
}

void SuggestionAgentClientImpl::EraseSelf() {
  suggestinator_->sources_.erase(component_url_);
}

}  // namespace suggestion
}  // namespace maxwell
