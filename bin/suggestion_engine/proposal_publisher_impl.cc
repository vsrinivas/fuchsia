// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/suggestion_engine/proposal_publisher_impl.h"
#include "apps/maxwell/src/suggestion_engine/ask_publisher.h"

namespace maxwell {

void ProposalPublisherImpl::Propose(ProposalPtr proposal) {
  engine_->AddNextProposal(this, std::move(proposal));
}

void ProposalPublisherImpl::Remove(const fidl::String& proposal_id) {
  engine_->RemoveProposal(component_url_, proposal_id);
}

void ProposalPublisherImpl::GetAll(const GetAllCallback& callback) {
  // TODO
}

void ProposalPublisherImpl::RegisterAskHandler(
    fidl::InterfaceHandle<AskHandler> ask_handler) {
  engine_->AddAskPublisher(std::make_unique<AskPublisher>(
      AskHandlerPtr::Create(std::move(ask_handler)),
      weak_ptr_factory_.GetWeakPtr()));
}

void ProposalPublisherImpl::BindingSet::OnConnectionError(
    fidl::Binding<ProposalPublisher>* binding) {
  maxwell::BindingSet<ProposalPublisher>::OnConnectionError(binding);

  if (impl_->ShouldEraseSelf())
    impl_->EraseSelf();
}

void ProposalPublisherImpl::EraseSelf() {
  engine_->RemoveSourceClient(component_url_);
}

}  // namespace maxwell
