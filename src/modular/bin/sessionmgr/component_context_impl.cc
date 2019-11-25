// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/component_context_impl.h"

#include <utility>

#include "src/lib/fxl/logging.h"
#include "src/modular/bin/sessionmgr/agent_runner/agent_runner.h"
#include "src/modular/lib/fidl/array_to_string.h"

namespace modular {

ComponentContextImpl::ComponentContextImpl(const ComponentContextInfo& info,
                                           std::string component_namespace,
                                           std::string component_instance_id,
                                           std::string component_url)
    : agent_runner_(info.agent_runner),
      entity_provider_runner_(info.entity_provider_runner),
      component_namespace_(std::move(component_namespace)),
      component_instance_id_(std::move(component_instance_id)),
      component_url_(std::move(component_url)) {
  FXL_DCHECK(agent_runner_);
  FXL_DCHECK(entity_provider_runner_);
}

ComponentContextImpl::~ComponentContextImpl() = default;

void ComponentContextImpl::Connect(
    fidl::InterfaceRequest<fuchsia::modular::ComponentContext> request) {
  bindings_.AddBinding(this, std::move(request));
}

fuchsia::modular::ComponentContextPtr ComponentContextImpl::NewBinding() {
  fuchsia::modular::ComponentContextPtr ptr;
  Connect(ptr.NewRequest());
  return ptr;
}

void ComponentContextImpl::ConnectToAgent(
    std::string url,
    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services_request,
    fidl::InterfaceRequest<fuchsia::modular::AgentController> agent_controller_request) {
  agent_runner_->ConnectToAgent(component_instance_id_, url, std::move(incoming_services_request),
                                std::move(agent_controller_request));
}

void ComponentContextImpl::ConnectToAgentService(fuchsia::modular::AgentServiceRequest request) {
  agent_runner_->ConnectToAgentService(component_instance_id_, std::move(request));
}

void ComponentContextImpl::GetEntityResolver(
    fidl::InterfaceRequest<fuchsia::modular::EntityResolver> request) {
  entity_provider_runner_->ConnectEntityResolver(std::move(request));
}

}  // namespace modular
