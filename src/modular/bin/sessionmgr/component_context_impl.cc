// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/component_context_impl.h"

#include <lib/syslog/cpp/macros.h>

#include <utility>

#include "src/modular/bin/sessionmgr/agent_runner/agent_runner.h"
#include "src/modular/lib/fidl/array_to_string.h"

namespace modular {
namespace {
void LogConnectionError(const std::string& url, const std::vector<std::string>& agents) {
  FX_LOGS(ERROR) << "Attempting to connect to agent " << url
                 << " which is not listed as a session agent.";
  FX_LOGS(ERROR) << "Session agents are:";
  for (const auto& agent : agents) {
    FX_LOGS(ERROR) << " - " << agent;
  }
  FX_LOGS(ERROR) << "To fix this error, please add " << url
                 << "to the modular configuration's 'session_agents'.";
}
}  // namespace

ComponentContextImpl::ComponentContextImpl(const ComponentContextInfo& info,
                                           std::string component_instance_id,
                                           std::string component_url)
    : agent_runner_(info.agent_runner),
      session_agents_(info.session_agents),
      component_instance_id_(std::move(component_instance_id)),
      component_url_(std::move(component_url)) {
  FX_DCHECK(agent_runner_);
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

void ComponentContextImpl::DeprecatedConnectToAgent(
    std::string url,
    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services_request,
    fidl::InterfaceRequest<fuchsia::modular::AgentController> agent_controller_request) {
  if (!AgentIsSessionAgent(url)) {
    LogConnectionError(url, session_agents_);
    return;
  }
  agent_runner_->ConnectToAgent(component_instance_id_, url, std::move(incoming_services_request),
                                std::move(agent_controller_request));
}

void ComponentContextImpl::DeprecatedConnectToAgentService(
    fuchsia::modular::AgentServiceRequest request) {
  if (request.has_handler() && !AgentIsSessionAgent(request.handler())) {
    LogConnectionError(request.handler(), session_agents_);
    return;
  }
  agent_runner_->ConnectToAgentService(component_instance_id_, std::move(request));
}

bool ComponentContextImpl::AgentIsSessionAgent(const std::string& agent_url) {
  return std::find(session_agents_.begin(), session_agents_.end(), agent_url) !=
         session_agents_.end();
}

}  // namespace modular
