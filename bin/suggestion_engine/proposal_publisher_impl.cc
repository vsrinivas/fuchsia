// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/proposal_publisher_impl.h"

#include "peridot/bin/suggestion_engine/suggestion_engine_impl.h"

namespace modular {

ProposalPublisherImpl::ProposalPublisherImpl(SuggestionEngineImpl* engine,
                                             const std::string& component_url)
    : engine_(engine),
      component_url_(component_url),
      bindings_(this),
      weak_ptr_factory_(this) {}

ProposalPublisherImpl::~ProposalPublisherImpl() = default;

void ProposalPublisherImpl::AddBinding(
    fidl::InterfaceRequest<fuchsia::modular::ProposalPublisher> request) {
  bindings_.emplace(new fidl::Binding<fuchsia::modular::ProposalPublisher>(
      this, std::move(request)));
}

void ProposalPublisherImpl::Propose(fuchsia::modular::Proposal proposal) {
  engine_->AddNextProposal(this, std::move(proposal));
}

void ProposalPublisherImpl::ProposeNavigation(
    fuchsia::modular::NavigationAction navigation) {
  engine_->ProposeNavigation(navigation);
}

void ProposalPublisherImpl::Remove(fidl::StringPtr proposal_id) {
  engine_->RemoveNextProposal(component_url_, proposal_id);
}

ProposalPublisherImpl::BindingSet::BindingSet(ProposalPublisherImpl* impl)
    : impl_(impl) {}

ProposalPublisherImpl::BindingSet::~BindingSet() = default;

void ProposalPublisherImpl::BindingSet::OnConnectionError(
    fidl::Binding<fuchsia::modular::ProposalPublisher>* binding) {
  ::modular::BindingSet<fuchsia::modular::ProposalPublisher>::OnConnectionError(
      binding);

  if (impl_->ShouldEraseSelf())
    impl_->EraseSelf();
}

bool ProposalPublisherImpl::ShouldEraseSelf() const {
  return bindings_.empty() && !weak_ptr_factory_.HasWeakPtrs();
}

void ProposalPublisherImpl::EraseSelf() {
  engine_->RemoveSourceClient(component_url_);
}

}  // namespace modular
