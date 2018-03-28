// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user/intelligence_services_impl.h"

#include <fuchsia/cpp/modular.h>

namespace maxwell {

IntelligenceServicesImpl::IntelligenceServicesImpl(
    modular::ComponentScope scope,
    modular::ContextEngine* context_engine,
    modular::SuggestionEngine* suggestion_engine,
    modular::UserActionLog* user_action_log)
    : scope_(std::move(scope)),
      context_engine_(context_engine),
      suggestion_engine_(suggestion_engine),
      user_action_log_(user_action_log) {}

modular::ComponentScope IntelligenceServicesImpl::CloneScope() {
  modular::ComponentScope scope;
  fidl::Clone(scope_, &scope);
  return scope;
}

void IntelligenceServicesImpl::GetContextReader(
    fidl::InterfaceRequest<modular::ContextReader> request) {
  context_engine_->GetReader(CloneScope(), std::move(request));
}

void IntelligenceServicesImpl::GetContextWriter(
    fidl::InterfaceRequest<modular::ContextWriter> request) {
  context_engine_->GetWriter(CloneScope(), std::move(request));
}

void IntelligenceServicesImpl::GetProposalPublisher(
    fidl::InterfaceRequest<modular::ProposalPublisher> request) {
  fidl::StringPtr component_id;
  if (scope_.is_agent_scope()) {
    component_id = scope_.agent_scope().url;
  } else if (scope_.is_module_scope()) {
    component_id = scope_.module_scope().url;
  } else {  // scope_.is_global_scope()
    component_id = "global";
  }

  // TODO(thatguy): Change |component_id| to use ComponentScope once it is
  // renamed to something like ComponentInfo.
  suggestion_engine_->RegisterProposalPublisher(component_id,
                                                std::move(request));
}

void IntelligenceServicesImpl::GetActionLog(
    fidl::InterfaceRequest<modular::ComponentActionLog> request) {
  user_action_log_->GetComponentActionLog(CloneScope(), std::move(request));
}

void IntelligenceServicesImpl::RegisterQueryHandler(
    fidl::InterfaceHandle<modular::QueryHandler> query_handler) {
  fidl::StringPtr component_id;
  if (scope_.is_agent_scope()) {
    component_id = scope_.agent_scope().url;
  } else if (scope_.is_module_scope()) {
    component_id = scope_.module_scope().url;
  } else {  // scope_.is_global_scope()
    component_id = "global";
  }

  suggestion_engine_->RegisterQueryHandler(component_id,
                                           std::move(query_handler));
}

}  // namespace maxwell
