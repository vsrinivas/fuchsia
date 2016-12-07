// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/suggestion_engine/proposal_publisher_impl.h"

namespace maxwell {

void ProposalPublisherImpl::Propose(ProposalPtr proposal) {
  SuggestionPrototype& suggestion_prototype = proposals_[proposal->id];

  if (suggestion_prototype.source) {
    // Asseert that we are registered; this isn't strictly necessary but makes
    // sense.
    FTL_DCHECK(suggestion_prototype.source == this);
    OnChangeProposal(std::move(proposal), &suggestion_prototype);
  } else {
    suggestion_prototype.source = this;
    suggestion_prototype.timestamp = ftl::TimePoint::Now();
    suggestion_prototype.proposal = std::move(proposal);
    repo_->AddSuggestion(&suggestion_prototype);
  }
}

void ProposalPublisherImpl::Remove(const fidl::String& proposal_id) {
  const auto it = proposals_.find(proposal_id);

  if (it != proposals_.end()) {
    for (auto channel_rank : it->second.ranks_by_channel) {
      channel_rank.first->OnRemoveSuggestion(channel_rank.second);
    }

    repo_->RemoveSuggestion(it->second.suggestion_id);
    proposals_.erase(it);

    if (ShouldEraseSelf())
      EraseSelf();
  }
}

void ProposalPublisherImpl::GetAll(const GetAllCallback& callback) {
  // TODO
}

void ProposalPublisherImpl::RegisterAskHandler(
    fidl::InterfaceHandle<AskHandler> ask_handler) {
  repo_->AddAskHandler(std::move(ask_handler));
}

void ProposalPublisherImpl::BindingSet::OnConnectionError(
    fidl::Binding<ProposalPublisher>* binding) {
  maxwell::BindingSet<ProposalPublisher>::OnConnectionError(binding);

  if (impl_->ShouldEraseSelf())
    impl_->EraseSelf();
}

void ProposalPublisherImpl::OnChangeProposal(
    ProposalPtr proposal,
    SuggestionPrototype* suggestion_prototype) {
  // TODO(rosswang): dedup

  suggestion_prototype->proposal = std::move(proposal);

  for (auto& channel_rank : suggestion_prototype->ranks_by_channel) {
    channel_rank.first->OnChangeSuggestion(channel_rank.second);
  }
}

void ProposalPublisherImpl::EraseSelf() {
  repo_->RemoveSourceClient(component_url_);
}

}  // namespace maxwell
