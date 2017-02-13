// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/agent_runner/agent_runner.h"

#include "application/lib/app/connect.h"
#include "apps/modular/src/agent_runner/agent_context_impl.h"

namespace modular {

AgentRunner::AgentRunner(app::ApplicationLauncher* application_launcher)
    : application_launcher_(application_launcher) {}

AgentRunner::~AgentRunner() = default;

void AgentRunner::ConnectToAgent(
    const std::string& requestor_url,
    const std::string& agent_url,
    fidl::InterfaceRequest<app::ServiceProvider> incoming_services_request,
    fidl::InterfaceRequest<AgentController> agent_controller_request) {
  auto found_it = running_agents_.find(agent_url);
  if (found_it == running_agents_.end()) {
    bool inserted = false;
    std::tie(found_it, inserted) = running_agents_.emplace(
        agent_url, std::make_unique<AgentContextImpl>(application_launcher_,
                                                      this, agent_url));
    FTL_DCHECK(inserted);
  }

  found_it->second->NewConnection(requestor_url,
                                  std::move(incoming_services_request),
                                  std::move(agent_controller_request));
}

void AgentRunner::RemoveAgent(const std::string& agent_url) {
  running_agents_.erase(agent_url);
}

}  // namespace modular
