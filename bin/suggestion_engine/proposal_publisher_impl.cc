// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/proposal_publisher_impl.h"

#include "peridot/bin/suggestion_engine/suggestion_engine_impl.h"

namespace maxwell {

ProposalPublisherImpl::ProposalPublisherImpl(SuggestionEngineImpl* engine,
                                             const std::string& component_url)
    : engine_(engine),
      component_url_(component_url),
      bindings_(this),
      weak_ptr_factory_(this) {}

ProposalPublisherImpl::~ProposalPublisherImpl() = default;

void ProposalPublisherImpl::AddBinding(
    f1dl::InterfaceRequest<ProposalPublisher> request) {
  bindings_.emplace(
      new f1dl::Binding<ProposalPublisher>(this, std::move(request)));
}

void ProposalPublisherImpl::Propose(ProposalPtr proposal) {
  engine_->AddNextProposal(this, std::move(proposal));
  engine_->UpdateRanking();
}

void ProposalPublisherImpl::Remove(const f1dl::String& proposal_id) {
  engine_->RemoveNextProposal(component_url_, proposal_id);
  engine_->UpdateRanking();
}

ProposalPublisherImpl::BindingSet::BindingSet(ProposalPublisherImpl* impl)
    : impl_(impl) {}

ProposalPublisherImpl::BindingSet::~BindingSet() = default;

void ProposalPublisherImpl::BindingSet::OnConnectionError(
    f1dl::Binding<ProposalPublisher>* binding) {
  maxwell::BindingSet<ProposalPublisher>::OnConnectionError(binding);

  if (impl_->ShouldEraseSelf())
    impl_->EraseSelf();
}

bool ProposalPublisherImpl::ShouldEraseSelf() const {
  return bindings_.empty() && !weak_ptr_factory_.HasWeakPtrs();
}

void ProposalPublisherImpl::EraseSelf() {
  engine_->RemoveSourceClient(component_url_);
}

}  // namespace maxwell
