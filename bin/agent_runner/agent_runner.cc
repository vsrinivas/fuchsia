// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/agent_runner/agent_runner.h"

#include "application/lib/app/connect.h"
#include "apps/modular/src/agent_runner/agent_context_impl.h"

namespace modular {

AgentRunner::AgentRunner(app::ApplicationLauncher* application_launcher,
                         MessageQueueManager* message_queue_manager,
                         ledger::LedgerRepository* ledger_repository)
    : application_launcher_(application_launcher),
      message_queue_manager_(message_queue_manager),
      ledger_repository_(ledger_repository) {}

AgentRunner::~AgentRunner() = default;

void AgentRunner::ConnectToAgent(
    const std::string& requestor_url,
    const std::string& agent_url,
    fidl::InterfaceRequest<app::ServiceProvider> incoming_services_request,
    fidl::InterfaceRequest<AgentController> agent_controller_request) {
  MaybeRunAgent(agent_url)->NewConnection(requestor_url,
                                          std::move(incoming_services_request),
                                          std::move(agent_controller_request));
}

void AgentRunner::RemoveAgent(const std::string& agent_url) {
  running_agents_.erase(agent_url);
}

void AgentRunner::ScheduleTask(const std::string& agent_url,
                               const std::string& task_id,
                               const std::string& queue_name) {
  auto found_it = scheduled_tasks_.find(agent_url);
  if (found_it == scheduled_tasks_.end()) {
    bool inserted = false;
    std::tie(found_it, inserted) = scheduled_tasks_.emplace(
        agent_url, std::unordered_map<std::string, std::string>());
    FTL_DCHECK(inserted);
  }

  found_it->second[task_id] = queue_name;
  message_queue_manager_->RegisterWatcher(
      agent_url, queue_name, [this, agent_url, task_id] {
        MaybeRunAgent(agent_url)->NewTask(task_id);
      });
}

void AgentRunner::DeleteTask(const std::string& agent_url,
                             const std::string& task_id) {
  auto agent_it = scheduled_tasks_.find(agent_url);
  if (agent_it == scheduled_tasks_.end()) {
    // Trying to delete a task which was not scheduled in the first place. Do
    // nothing.
    return;
  }

  auto& agent_map = agent_it->second;
  auto task_id_it = agent_map.find(task_id);
  if (task_id_it == agent_map.end()) {
    // Trying to delete a task which was not scheduled in the first place. Do
    // nothing.
    return;
  }

  message_queue_manager_->DropWatcher(agent_url, task_id_it->second);
  scheduled_tasks_[agent_url].erase(task_id);
}

AgentContextImpl* AgentRunner::MaybeRunAgent(const std::string& agent_url) {
  auto found_it = running_agents_.find(agent_url);
  if (found_it == running_agents_.end()) {
    bool inserted = false;
    std::tie(found_it, inserted) = running_agents_.emplace(
        agent_url, std::make_unique<AgentContextImpl>(
                       application_launcher_, message_queue_manager_, this,
                       ledger_repository_, agent_url));
    FTL_DCHECK(inserted);
  }

  return found_it->second.get();
}

}  // namespace modular
