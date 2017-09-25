// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/component/component_context_impl.h"

#include <utility>

#include "lib/fxl/logging.h"
#include "peridot/bin/agent_runner/agent_runner.h"
#include "peridot/lib/fidl/array_to_string.h"

namespace modular {

ComponentContextImpl::ComponentContextImpl(const ComponentContextInfo& info,
                                           std::string component_namespace,
                                           std::string component_instance_id,
                                           std::string component_url)
    : message_queue_manager_(info.message_queue_manager),
      agent_runner_(info.agent_runner),
      ledger_repository_(info.ledger_repository),
      entity_repository_(info.entity_repository),
      component_namespace_(std::move(component_namespace)),
      component_instance_id_(std::move(component_instance_id)),
      component_url_(std::move(component_url)) {
  FXL_DCHECK(message_queue_manager_);
  FXL_DCHECK(agent_runner_);
  FXL_DCHECK(ledger_repository_);
  FXL_DCHECK(entity_repository_);
}

ComponentContextImpl::~ComponentContextImpl() = default;

void ComponentContextImpl::GetLedger(
    fidl::InterfaceRequest<ledger::Ledger> request,
    const GetLedgerCallback& result) {
  ledger_repository_->GetLedger(to_array(component_url_), std::move(request),
                                result);
}

void ComponentContextImpl::ConnectToAgent(
    const fidl::String& url,
    fidl::InterfaceRequest<app::ServiceProvider> incoming_services_request,
    fidl::InterfaceRequest<AgentController> agent_controller_request) {
  agent_runner_->ConnectToAgent(component_instance_id_, url,
                                std::move(incoming_services_request),
                                std::move(agent_controller_request));
}

void ComponentContextImpl::ObtainMessageQueue(
    const fidl::String& name,
    fidl::InterfaceRequest<MessageQueue> request) {
  message_queue_manager_->ObtainMessageQueue(
      component_namespace_, component_instance_id_, name, std::move(request));
}

void ComponentContextImpl::DeleteMessageQueue(const fidl::String& name) {
  message_queue_manager_->DeleteMessageQueue(component_namespace_,
                                             component_instance_id_, name);
}

void ComponentContextImpl::GetMessageSender(
    const fidl::String& queue_token,
    fidl::InterfaceRequest<MessageSender> request) {
  message_queue_manager_->GetMessageSender(queue_token, std::move(request));
}

void ComponentContextImpl::GetEntityStore(
    fidl::InterfaceRequest<EntityStore> request) {
  entity_repository_->ConnectEntityStore(std::move(request));
}

void ComponentContextImpl::GetEntityResolver(
    fidl::InterfaceRequest<EntityResolver> request) {
  entity_repository_->ConnectEntityResolver(std::move(request));
}

}  // namespace modular
