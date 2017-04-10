// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_AGENT_RUNNER_AGENT_RUNNER_H_
#define APPS_MODULAR_SRC_AGENT_RUNNER_AGENT_RUNNER_H_

#include <memory>
#include <string>
#include <unordered_map>

#include "application/services/application_launcher.fidl.h"
#include "application/services/service_provider.fidl.h"
#include "apps/ledger/services/internal/internal.fidl.h"
#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/maxwell/services/user/user_intelligence_provider.fidl.h"
#include "apps/modular/lib/fidl/operation.h"
#include "apps/modular/lib/fidl/page_client.h"
#include "apps/modular/services/agent/agent_context.fidl.h"
#include "apps/modular/services/agent/agent_controller/agent_controller.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/macros.h"

namespace modular {

class AgentContextImpl;
class MessageQueueManager;
class XdrContext;

// This class provides a way for components to connect to agents and
// manages the life time of a running agent.
class AgentRunner : ledger::PageWatcher {
 public:
  AgentRunner(app::ApplicationLauncher* application_launcher,
              MessageQueueManager* message_queue_manager,
              ledger::LedgerRepository* ledger_repository,
              ledger::PagePtr page,
              maxwell::UserIntelligenceProvider* user_intelligence_provider);
  ~AgentRunner();

  // |callback| is called after - (1) all agents have been shutdown and (2)
  // no new tasks are scheduled to run.
  void Teardown(const std::function<void()>& callback);

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
  // trigger condition specified in |task_info| is satisfied. The trigger
  // condition is also replicated to the ledger and the task my get scheduled on
  // other user devices too.
  void ScheduleTask(const std::string& agent_url, TaskInfoPtr task_info);

  // Deletes a task for |agent_url| that is identified by agent provided
  // |task_id|. The trigger condition is removed from the ledger.
  void DeleteTask(const std::string& agent_url, const std::string& task_id);

 private:
  struct TriggerInfo;

  static void XdrTriggerInfo(XdrContext* const xdr, TriggerInfo* const data);

  AgentContextImpl* MaybeRunAgent(const std::string& agent_url);

  void AddedTrigger(const std::string& key, std::string value);
  void DeletedTrigger(const std::string& key);

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

  // |PageWatcher|
  void OnChange(ledger::PageChangePtr page,
                ledger::ResultState result_state,
                const OnChangeCallback& callback) override;

  // agent URL -> { task id -> queue name }
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
      watched_queues_;

  // agent URL -> { task id -> alarm in seconds }
  std::unordered_map<std::string, std::unordered_map<std::string, uint32_t>>
      running_alarms_;

  // agent URL -> modular.AgentContext
  std::unordered_map<std::string, std::unique_ptr<AgentContextImpl>>
      running_agents_;

  // ledger key -> [agent URL, task ID]
  //
  // Used to delete entries from the maps above when a ledger key is
  // deleted. This saves us from having to parse a legder key, which
  // becomes impossible once we use hashes to construct it, or from
  // having to read the value from the previous snapshot, which would
  // be nifty but is easy only once we have Operations.
  std::unordered_map<std::string, std::pair<std::string, std::string>>
      task_by_ledger_key_;

  app::ApplicationLauncher* const application_launcher_;
  MessageQueueManager* const message_queue_manager_;
  ledger::LedgerRepository* const ledger_repository_;
  ledger::PagePtr page_;
  maxwell::UserIntelligenceProvider* const user_intelligence_provider_;

  // A watcher for any changes happening to the trigger list on the ledger.
  fidl::Binding<ledger::PageWatcher> watcher_binding_;
  PageClient page_client_;

  // When this is marked true, no new new tasks will be scheduled.
  std::shared_ptr<bool> terminating_;

  OperationQueue operation_queue_;

  // Operations implemented here.
  class InitializeCall;
  class UpdateCall;

  FTL_DISALLOW_COPY_AND_ASSIGN(AgentRunner);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_AGENT_RUNNER_AGENT_RUNNER_H_
