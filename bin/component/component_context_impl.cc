// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/component/component_context_impl.h"

#include "apps/modular/src/agent_runner/agent_runner.h"
#include "lib/ftl/logging.h"

namespace modular {

ComponentContextImpl::ComponentContextImpl(
    MessageQueueManager* const message_queue_manager,
    AgentRunner* const agent_runner,
    const std::string& component_id)
    : message_queue_manager_(message_queue_manager),
      agent_runner_(agent_runner),
      component_id_(component_id) {
  FTL_DCHECK(agent_runner);
}

ComponentContextImpl::~ComponentContextImpl() = default;

void ComponentContextImpl::ConnectToAgent(
    const fidl::String& url,
    fidl::InterfaceRequest<app::ServiceProvider> incoming_services_request,
    fidl::InterfaceRequest<AgentController> agent_controller_request) {
  agent_runner_->ConnectToAgent(component_id_, url,
                                std::move(incoming_services_request),
                                std::move(agent_controller_request));
}

void ComponentContextImpl::ObtainMessageQueue(
    const fidl::String& name,
    fidl::InterfaceRequest<MessageQueue> request) {
  message_queue_manager_->ObtainMessageQueue(component_id_, name,
                                             std::move(request));
}

void ComponentContextImpl::DeleteMessageQueue(const fidl::String& name) {
  message_queue_manager_->DeleteMessageQueue(component_id_, name);
}

void ComponentContextImpl::GetMessageSender(
    const fidl::String& queue_token,
    fidl::InterfaceRequest<MessageSender> request) {
  message_queue_manager_->GetMessageSender(queue_token, std::move(request));
}

}  // namespace modular
