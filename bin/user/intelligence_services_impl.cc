// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/user/intelligence_services_impl.h"

#include "apps/maxwell/services/context/context_engine.fidl.h"
#include "apps/maxwell/services/suggestion/suggestion_engine.fidl.h"

namespace maxwell {

IntelligenceServicesImpl::IntelligenceServicesImpl(
    const std::string& story_id, const std::string& component_id,
    ContextEngine* context_engine, SuggestionEngine* suggestion_engine)
    : story_id_(story_id),
      component_id_(component_id),
      context_engine_(context_engine),
      suggestion_engine_(suggestion_engine) {}

void IntelligenceServicesImpl::GetContextProvider(
    fidl::InterfaceRequest<ContextSubscriber> request) {
  context_engine_->RegisterSubscriber(component_id_, std::move(request));
}

void IntelligenceServicesImpl::GetContextPublisher(
    fidl::InterfaceRequest<ContextPublisher> request) {
  context_engine_->RegisterPublisher(component_id_, std::move(request));
}

void IntelligenceServicesImpl::GetProposalPublisher(
    fidl::InterfaceRequest<ProposalPublisher> request) {
  suggestion_engine_->RegisterPublisher(component_id_, std::move(request));
}

}  // namespace maxwell
