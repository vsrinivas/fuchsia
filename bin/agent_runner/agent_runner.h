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
#include "apps/ledger/services/internal/internal.fidl.h"
#include "apps/modular/services/agent/agent_controller/agent_controller.fidl.h"
#include "lib/ftl/macros.h"

namespace modular {

class AgentContextImpl;
class MessageQueueManager;

// This class provides a way for components to connect to agents and manages the
// life time of a running agent.
class AgentRunner {
 public:
  AgentRunner(app::ApplicationLauncher* application_launcher,
              MessageQueueManager* message_queue_manager,
              ledger::LedgerRepository* ledger_repository);
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

  // Agent at |agent_url| is run (if not already running) and Agent.RunTask() is
  // called with |task_id| as the agent specified identfier for the task when a
  // new message is received on |queue_name|.
  // NOTE(alhaad): The current implementation only allows for a single task
  // to be associated with a MessageQueue. This method fails if a new task
  // is scheduled for a queue which already has another task scheduled.
  void ScheduleTask(const std::string& agent_url,
                    const std::string& task_id,
                    const std::string& queue_name);

  // Deletes a task for |agent_url| that is identified by agent provided
  // |task_id|.
  void DeleteTask(const std::string& agent_url, const std::string& task_id);

 private:
  AgentContextImpl* MaybeRunAgent(const std::string& agent_url);

  // agent URL -> modular.AgentContext
  std::unordered_map<std::string, std::unique_ptr<AgentContextImpl>>
      running_agents_;

  // agent URL -> { task id -> queue name }
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
      scheduled_tasks_;

  app::ApplicationLauncher* const application_launcher_;
  MessageQueueManager* const message_queue_manager_;
  ledger::LedgerRepository* const ledger_repository_;

  FTL_DISALLOW_COPY_AND_ASSIGN(AgentRunner);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_AGENT_RUNNER_AGENT_RUNNER_H_
