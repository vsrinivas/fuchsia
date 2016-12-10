// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/suggestion_engine/proposal_publisher_impl.h"

namespace maxwell {

void ProposalPublisherImpl::Propose(ProposalPtr proposal) {
  std::unique_ptr<SuggestionPrototype>& suggestion_prototype =
      proposals_[proposal->id];

  if (suggestion_prototype) {
    FTL_DCHECK(suggestion_prototype->source == this);
    OnChangeProposal(std::move(proposal), suggestion_prototype.get());
  } else {
    suggestion_prototype = std::make_unique<SuggestionPrototype>();
    suggestion_prototype->source = this;
    suggestion_prototype->timestamp = ftl::TimePoint::Now();
    suggestion_prototype->proposal = std::move(proposal);
    repo_->AddSuggestion(suggestion_prototype.get());
  }
}

void ProposalPublisherImpl::Remove(const fidl::String& proposal_id) {
  Extract(proposal_id);
}

void ProposalPublisherImpl::GetAll(const GetAllCallback& callback) {
  // TODO
}

void ProposalPublisherImpl::RegisterAskHandler(
    fidl::InterfaceHandle<AskHandler> ask_handler) {
  repo_->AddAskHandler(std::move(ask_handler));
}

std::unique_ptr<SuggestionPrototype> ProposalPublisherImpl::Extract(
    const std::string& proposal_id) {
  const auto it = proposals_.find(proposal_id);

  if (it == proposals_.end())
    return NULL;
  for (auto channel_rank : it->second->ranks_by_channel) {
    channel_rank.first->OnRemoveSuggestion(channel_rank.second);
  }
  repo_->RemoveSuggestion(it->second->suggestion_id);
  std::unique_ptr<SuggestionPrototype> extracted_proposal =
      std::move(it->second);
  proposals_.erase(it);

  if (ShouldEraseSelf())
    EraseSelf();
  return extracted_proposal;
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
