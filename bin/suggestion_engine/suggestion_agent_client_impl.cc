// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/suggestion_engine/suggestion_agent_client_impl.h"

namespace maxwell {
namespace suggestion {

void SuggestionAgentClientImpl::Propose(ProposalPtr proposal) {
  const size_t old_size = proposals_.size();
  AgentSuggestionRecord* record = &proposals_[proposal->id];

  if (proposals_.size() > old_size)
    repo_->AddSuggestion(
        std::make_unique<ProposalRecord>(this, std::move(proposal)), record);
  else
    OnChangeProposal(std::move(proposal), record);
}

void SuggestionAgentClientImpl::Remove(const fidl::String& proposal_id) {
  const auto record = proposals_.find(proposal_id);

  if (record != proposals_.end()) {
    for (auto& channel_rank : record->second.ranks_by_channel) {
      channel_rank.first->OnRemoveSuggestion(channel_rank.second);
    }

    repo_->RemoveSuggestion(record->second.suggestion_prototype->first);
    proposals_.erase(record);

    if (ShouldEraseSelf())
      EraseSelf();
  }
}

void SuggestionAgentClientImpl::GetAll(const GetAllCallback& callback) {
  // TODO
}

void SuggestionAgentClientImpl::RegisterAskHandler(
    fidl::InterfaceHandle<AskHandler> ask_handler) {
  repo_->AddAskHandler(std::move(ask_handler));
}

void SuggestionAgentClientImpl::BindingSet::OnConnectionError(
    fidl::Binding<SuggestionAgentClient>* binding) {
  maxwell::BindingSet<SuggestionAgentClient>::OnConnectionError(binding);

  if (impl_->ShouldEraseSelf())
    impl_->EraseSelf();
}

void SuggestionAgentClientImpl::OnChangeProposal(
    ProposalPtr proposal,
    AgentSuggestionRecord* record) {
  // TODO(rosswang): dedup

  record->suggestion_prototype->second->proposal = std::move(proposal);

  for (auto& channel_rank : record->ranks_by_channel) {
    channel_rank.first->OnChangeSuggestion(channel_rank.second);
  }
}

void SuggestionAgentClientImpl::EraseSelf() {
  repo_->RemoveSourceClient(component_url_);
}

}  // namespace suggestion
}  // namespace maxwell
