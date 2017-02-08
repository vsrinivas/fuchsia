// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/component/component_context_impl.h"

#include "apps/modular/src/agent_runner/agent_runner.h"
#include "lib/ftl/logging.h"

namespace modular {

ComponentContextImpl::ComponentContextImpl(const std::string& component_id,
                                           AgentRunner* const agent_runner)
    : component_id_(component_id), agent_runner_(agent_runner) {
  FTL_DCHECK(agent_runner);
}

ComponentContextImpl::~ComponentContextImpl() = default;

void ComponentContextImpl::ConnectToAgent(
    const fidl::String& url,
    fidl::InterfaceRequest<ServiceProvider> incoming_services_request,
    fidl::InterfaceRequest<AgentController> agent_controller_request) {
  // TODO: Plumb requestor url.
  agent_runner_->ConnectToAgent(
      component_id_, url, std::move(incoming_services_request),
      std::move(agent_controller_request));
}

void ComponentContextImpl::ObtainMessageQueue(
    const fidl::String& name,
    fidl::InterfaceRequest<MessageQueue> request) {
  // Unimplemented.
  FTL_LOG(INFO) << "ComponentContextImpl::ObtainMessageQueue";
}

void ComponentContextImpl::DeleteMessageQueue(const fidl::String& name) {
  // Unimplemented.
  FTL_LOG(INFO) << "ComponentContextImpl::DeleteMessageQueue";
}

void ComponentContextImpl::GetMessageSender(
    const fidl::String& queue_token,
    fidl::InterfaceRequest<MessageSender> request) {
  // Unimplemented.
  FTL_LOG(INFO) << "ComponentContextImpl::GetMessageSender";
}

}  // namespace modular
