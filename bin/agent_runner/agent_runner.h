// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_AGENT_RUNNER_AGENT_RUNNER_H_
#define APPS_MODULAR_SRC_AGENT_RUNNER_AGENT_RUNNER_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "application/services/application_launcher.fidl.h"
#include "application/services/service_provider.fidl.h"
#include "apps/modular/services/agent/agent_controller/agent_controller.fidl.h"
#include "lib/ftl/macros.h"

namespace modular {

class AgentContextImpl;

// This class provides a way for components to connect to agents and manages the
// life time of a running agent.
class AgentRunner {
 public:
  explicit AgentRunner(app::ApplicationLauncher* application_launcher);
  ~AgentRunner();

  // Connects to an agent (and starts it up if it doesn't exist). Called via
  // ComponentContext.
  void ConnectToAgent(
      const std::string& requestor_url,
      const std::string& agent_url,
      fidl::InterfaceRequest<app::ServiceProvider> incoming_services_request,
      fidl::InterfaceRequest<AgentController> agent_controller_request);

  // Removes an agent. Called by AgentContextImpl when it is done.
  void RemoveAgent(const std::string& agent_url);

 private:
  // agent URL -> modular.AgentContext
  std::unordered_map<std::string, std::unique_ptr<AgentContextImpl>>
      running_agents_;

  app::ApplicationLauncher* const application_launcher_;

  FTL_DISALLOW_COPY_AND_ASSIGN(AgentRunner);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_AGENT_RUNNER_AGENT_RUNNER_H_
