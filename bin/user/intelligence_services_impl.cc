// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user/intelligence_services_impl.h"

#include "lib/action_log/fidl/component.fidl.h"
#include "lib/action_log/fidl/user.fidl.h"
#include "lib/context/fidl/context_engine.fidl.h"
#include "lib/suggestion/fidl/suggestion_engine.fidl.h"

namespace maxwell {

IntelligenceServicesImpl::IntelligenceServicesImpl(
    ComponentScopePtr scope,
    ContextEngine* context_engine,
    SuggestionEngine* suggestion_engine,
    UserActionLog* user_action_log)
    : scope_(std::move(scope)),
      context_engine_(context_engine),
      suggestion_engine_(suggestion_engine),
      user_action_log_(user_action_log) {}

void IntelligenceServicesImpl::GetContextReader(
    fidl::InterfaceRequest<ContextReader> request) {
  context_engine_->GetReader(scope_.Clone(), std::move(request));
}

void IntelligenceServicesImpl::GetContextWriter(
    fidl::InterfaceRequest<ContextWriter> request) {
  context_engine_->GetWriter(scope_.Clone(), std::move(request));
}

void IntelligenceServicesImpl::GetProposalPublisher(
    fidl::InterfaceRequest<ProposalPublisher> request) {
  fidl::String component_id;
  if (scope_->is_agent_scope()) {
    component_id = scope_->get_agent_scope()->url;
  } else if (scope_->is_module_scope()) {
    component_id = scope_->get_module_scope()->url;
  } else {  // scope_->is_global_scope()
    component_id = "global";
  }

  // TODO(thatguy): Change |component_id| to use ComponentScope once it is
  // renamed to something like ComponentInfo.
  suggestion_engine_->RegisterProposalPublisher(component_id, std::move(request));
}

void IntelligenceServicesImpl::GetActionLog(
    fidl::InterfaceRequest<ComponentActionLog> request) {
  user_action_log_->GetComponentActionLog(scope_.Clone(), std::move(request));
}

void IntelligenceServicesImpl::RegisterQueryHandler(
    fidl::InterfaceHandle<QueryHandler> query_handler) {
  fidl::String component_id;
  if (scope_->is_agent_scope()) {
    component_id = scope_->get_agent_scope()->url;
  } else if (scope_->is_module_scope()) {
    component_id = scope_->get_module_scope()->url;
  } else {  // scope_->is_global_scope()
    component_id = "global";
  }

  suggestion_engine_->RegisterQueryHandler(component_id,
      std::move(query_handler));
}

}  // namespace maxwell
