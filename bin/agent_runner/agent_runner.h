// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_AGENT_RUNNER_AGENT_RUNNER_H_
#define PERIDOT_BIN_AGENT_RUNNER_AGENT_RUNNER_H_

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "lib/agent/fidl/agent_context.fidl.h"
#include "lib/agent/fidl/agent_controller/agent_controller.fidl.h"
#include "lib/agent/fidl/agent_provider.fidl.h"
#include "lib/app/fidl/application_launcher.fidl.h"
#include "lib/app/fidl/service_provider.fidl.h"
#include "lib/async/cpp/operation.h"
#include "lib/auth/fidl/account_provider.fidl.h"
#include "lib/entity/fidl/entity_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/fxl/macros.h"
#include "lib/ledger/fidl/ledger.fidl.h"
#include "lib/user_intelligence/fidl/user_intelligence_provider.fidl.h"
#include "peridot/bin/agent_runner/agent_runner_storage.h"
#include "peridot/bin/ledger/fidl/internal.fidl.h"

namespace modular {

// This is the component namespace we give to all agents; used for namespacing
// storage between different component types.
constexpr char kAgentComponentNamespace[] = "agents";

class AgentContextImpl;
class EntityProviderRunner;
class MessageQueueManager;
class XdrContext;

// This class provides a way for components to connect to agents and
// manages the life time of a running agent.
class AgentRunner : AgentProvider, AgentRunnerStorage::NotificationDelegate {
 public:
  AgentRunner(app::ApplicationLauncher* application_launcher,
              MessageQueueManager* message_queue_manager,
              ledger::LedgerRepository* ledger_repository,
              AgentRunnerStorage* agent_runner_storage,
              auth::TokenProviderFactory* token_provider_factory,
              maxwell::UserIntelligenceProvider* user_intelligence_provider,
              EntityProviderRunner* const entity_provider_runner);
  ~AgentRunner() override;

  void Connect(fidl::InterfaceRequest<AgentProvider> request);

  // |callback| is called after - (1) all agents have been shutdown and (2)
  // no new tasks are scheduled to run.
  void Teardown(const std::function<void()>& callback);

  // Connects to an agent (and starts it up if it doesn't exist) through
  // |Agent.Connect|. Called using ComponentContext.
  void ConnectToAgent(
      const std::string& requestor_url,
      const std::string& agent_url,
      fidl::InterfaceRequest<app::ServiceProvider> incoming_services_request,
      fidl::InterfaceRequest<AgentController> agent_controller_request);

  // Connects to an agent (and starts it up if it doesn't exist) through its
  // |EntityProvider| service.
  void ConnectToEntityProvider(
      const std::string& agent_url,
      fidl::InterfaceRequest<EntityProvider> entity_provider_request,
      fidl::InterfaceRequest<AgentController> agent_controller_request);

  // Removes an agent. Called by AgentContextImpl when it is done.
  // NOTE: This should NOT take a const reference, since |agent_url| will die
  // the moment we delete |AgentContextImpl|.
  void RemoveAgent(std::string agent_url);

  // Agent at |agent_url| is run (if not already running) and Agent.RunTask() is
  // called with |task_id| as the agent specified identifier for the task when a
  // trigger condition specified in |task_info| is satisfied. The trigger
  // condition is also replicated to the ledger and the task my get scheduled on
  // other user devices too.
  void ScheduleTask(const std::string& agent_url, TaskInfoPtr task_info);

  // Deletes a task for |agent_url| that is identified by agent provided
  // |task_id|. The trigger condition is removed from the ledger.
  void DeleteTask(const std::string& agent_url, const std::string& task_id);

 private:
  // Starts up an agent, or waits until the agent can start up if it is already
  // in a terminating state. Calls |done| once the agent has started.
  // Note that the agent could be in an INITIALIZING state.
  void MaybeRunAgent(const std::string& agent_url,
                     const std::function<void()>& done);

  // Actually starts up an agent (used by |MaybeRunAgent()| above).
  void RunAgent(const std::string& agent_url);

  // Will also start and initialize the agent as a consequence.
  void ForwardConnectionsToAgent(const std::string& agent_url);

  // For triggers based on message queues.
  void ScheduleMessageQueueTask(const std::string& agent_url,
                                const std::string& task_id,
                                const std::string& queue_name);
  void DeleteMessageQueueTask(const std::string& agent_url,
                              const std::string& task_id);

  // For triggers based on alarms.
  void ScheduleAlarmTask(const std::string& agent_url,
                         const std::string& task_id,
                         uint32_t alarm_in_seconds,
                         bool is_new_request);
  void DeleteAlarmTask(const std::string& agent_url,
                       const std::string& task_id);

  // A set of all agents that are either running or scheduled to be run.
  fidl::Array<fidl::String> GetAllAgents();

  // |UpdateWatchers| will not notify watchers if we are tearing down.
  void UpdateWatchers();

  // |AgentProvider|
  void Watch(fidl::InterfaceHandle<AgentProviderWatcher> watcher) override;

  // |AgentRunnerStorage::Delegate|
  void AddedTask(const std::string& key,
                 AgentRunnerStorage::TriggerInfo data) override;

  // |AgentRunnerStorage::Delegate|
  void DeletedTask(const std::string& key) override;

  // agent URL -> { task id -> queue name }
  std::map<std::string, std::map<std::string, std::string>> watched_queues_;

  // agent URL -> { task id -> alarm in seconds }
  std::map<std::string, std::map<std::string, uint32_t>> running_alarms_;

  // agent URL -> pending agent connections
  // This map holds connections to an agent that we hold onto while the existing
  // agent is in a terminating state.
  struct PendingAgentConnectionEntry {
    const std::string requestor_url;
    fidl::InterfaceRequest<app::ServiceProvider> incoming_services_request;
    fidl::InterfaceRequest<AgentController> agent_controller_request;
  };
  std::map<std::string, std::vector<struct PendingAgentConnectionEntry>>
      pending_agent_connections_;

  // agent URL -> pending entity provider connection
  // This map holds connections to an agents' EntityProvider that we hold onto
  // while the existing agent is in a terminating state.
  struct PendingEntityProviderConnectionEntry {
    fidl::InterfaceRequest<EntityProvider> entity_provider_request;
    fidl::InterfaceRequest<AgentController> agent_controller_request;
  };
  std::map<std::string, struct PendingEntityProviderConnectionEntry>
      pending_entity_provider_connections_;

  // agent URL -> done callbacks to invoke once agent has started.
  // Holds requests to start an agent; in case an agent is already in a
  // terminating state, we pend those requests here until the agent terminates.
  std::map<std::string, std::vector<std::function<void()>>>
      run_agent_callbacks_;

  // agent URL -> modular.AgentContext
  std::map<std::string, std::unique_ptr<AgentContextImpl>> running_agents_;

  // ledger key -> [agent URL, task ID]
  //
  // Used to delete entries from the maps above when a ledger key is
  // deleted. This saves us from having to parse a ledger key, which
  // becomes impossible once we use hashes to construct it, or from
  // having to read the value from the previous snapshot, which would
  // be nifty but is easy only once we have Operations.
  std::map<std::string, std::pair<std::string, std::string>>
      task_by_ledger_key_;

  app::ApplicationLauncher* const application_launcher_;
  MessageQueueManager* const message_queue_manager_;
  ledger::LedgerRepository* const ledger_repository_;
  // |agent_runner_storage_| must outlive this class.
  AgentRunnerStorage* const agent_runner_storage_;
  auth::TokenProviderFactory* const token_provider_factory_;
  maxwell::UserIntelligenceProvider* const user_intelligence_provider_;
  EntityProviderRunner* const entity_provider_runner_;

  fidl::BindingSet<AgentProvider> agent_provider_bindings_;
  fidl::InterfacePtrSet<AgentProviderWatcher> agent_provider_watchers_;

  // When this is marked true, no new new tasks will be scheduled.
  std::shared_ptr<bool> terminating_;
  // This is called as part of the |StopForTeardown()| flow, when the last agent
  // is torn down.
  std::function<void()> termination_callback_;

  OperationQueue operation_queue_;

  // Operations implemented here.
  class InitializeCall;
  class UpdateCall;
  class DeleteCall;

  FXL_DISALLOW_COPY_AND_ASSIGN(AgentRunner);
};

}  // namespace modular

#endif  // PERIDOT_BIN_AGENT_RUNNER_AGENT_RUNNER_H_
