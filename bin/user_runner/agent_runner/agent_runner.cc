// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/agent_runner/agent_runner.h"

#include <map>
#include <set>
#include <utility>

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/functional/make_copyable.h"
#include "peridot/bin/user_runner/agent_runner/agent_context_impl.h"
#include "peridot/bin/user_runner/agent_runner/agent_runner_storage_impl.h"
#include "peridot/bin/user_runner/storage/constants_and_utils.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/fidl/json_xdr.h"

namespace modular {

constexpr zx::duration kTeardownTimeout = zx::sec(3);

AgentRunner::AgentRunner(
    fuchsia::sys::Launcher* const launcher,
    MessageQueueManager* const message_queue_manager,
    fuchsia::ledger::internal::LedgerRepository* const ledger_repository,
    AgentRunnerStorage* const agent_runner_storage,
    fuchsia::modular::auth::TokenProviderFactory* const token_provider_factory,
    fuchsia::modular::UserIntelligenceProvider* const
        user_intelligence_provider,
    EntityProviderRunner* const entity_provider_runner)
    : launcher_(launcher),
      message_queue_manager_(message_queue_manager),
      ledger_repository_(ledger_repository),
      agent_runner_storage_(agent_runner_storage),
      token_provider_factory_(token_provider_factory),
      user_intelligence_provider_(user_intelligence_provider),
      entity_provider_runner_(entity_provider_runner),
      terminating_(std::make_shared<bool>(false)) {
  agent_runner_storage_->Initialize(this, [] {});
}

AgentRunner::~AgentRunner() = default;

void AgentRunner::Connect(
    fidl::InterfaceRequest<fuchsia::modular::AgentProvider> request) {
  agent_provider_bindings_.AddBinding(this, std::move(request));
}

void AgentRunner::Teardown(const std::function<void()>& callback) {
  // No new agents will be scheduled to run.
  *terminating_ = true;

  FXL_LOG(INFO) << "AgentRunner::Teardown() " << running_agents_.size()
                << " agents";

  // No agents were running, we are good to go.
  if (running_agents_.empty()) {
    callback();
    return;
  }

  // This is called when agents are done being removed
  auto called = std::make_shared<bool>(false);
  auto cont = [this, called, callback](const bool from_timeout) {
    if (*called) {
      return;
    }

    *called = true;

    if (from_timeout) {
      FXL_LOG(ERROR) << "AgentRunner::Teardown() timed out";
    }

    callback();
  };

  termination_callback_ = [cont] { cont(false); };

  for (auto& it : running_agents_) {
    // The running agent will call |AgentRunner::RemoveAgent()| to remove itself
    // from the agent runner. When all agents are done being removed,
    // |RemoveAgent()| will call |termination_callback_|.
    it.second->StopForTeardown();
  }

  auto cont_timeout = [cont] { cont(true); };

  async::PostDelayedTask(async_get_default(), std::move(cont_timeout),
                         kTeardownTimeout);
}

void AgentRunner::MaybeRunAgent(const std::string& agent_url,
                                const std::function<void()>& done) {
  auto agent_it = running_agents_.find(agent_url);
  if (agent_it != running_agents_.end()) {
    if (agent_it->second->state() == AgentContextImpl::State::TERMINATING) {
      run_agent_callbacks_[agent_url].push_back(done);
      return;
    }
    // fuchsia::modular::Agent is already running, so we can issue the callback
    // immediately.
    done();
    return;
  }

  run_agent_callbacks_[agent_url].push_back(done);

  RunAgent(agent_url);
}

void AgentRunner::RunAgent(const std::string& agent_url) {
  // Start the agent and issue all callbacks.
  ComponentContextInfo component_info = {message_queue_manager_, this,
                                         ledger_repository_,
                                         entity_provider_runner_};
  AgentContextInfo info = {component_info, launcher_, token_provider_factory_,
                           user_intelligence_provider_};
  fuchsia::modular::AppConfig agent_config;
  agent_config.url = agent_url;

  FXL_CHECK(running_agents_
                .emplace(agent_url, std::make_unique<AgentContextImpl>(
                                        info, std::move(agent_config)))
                .second);

  auto run_callbacks_it = run_agent_callbacks_.find(agent_url);
  if (run_callbacks_it != run_agent_callbacks_.end()) {
    for (auto& callback : run_callbacks_it->second) {
      callback();
    }
    run_agent_callbacks_.erase(agent_url);
  }

  UpdateWatchers();
}

void AgentRunner::ConnectToAgent(
    const std::string& requestor_url, const std::string& agent_url,
    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider>
        incoming_services_request,
    fidl::InterfaceRequest<fuchsia::modular::AgentController>
        agent_controller_request) {
  // Drop all new requests if AgentRunner is terminating.
  if (*terminating_) {
    return;
  }

  pending_agent_connections_[agent_url].push_back(
      {requestor_url, std::move(incoming_services_request),
       std::move(agent_controller_request)});

  MaybeRunAgent(agent_url, [this, agent_url] {
    // If the agent was terminating and has restarted, forwarding connections
    // here is redundant, since it was already forwarded earlier.
    ForwardConnectionsToAgent(agent_url);
  });
}

void AgentRunner::ConnectToEntityProvider(
    const std::string& agent_url,
    fidl::InterfaceRequest<fuchsia::modular::EntityProvider>
        entity_provider_request,
    fidl::InterfaceRequest<fuchsia::modular::AgentController>
        agent_controller_request) {
  // Drop all new requests if AgentRunner is terminating.
  if (*terminating_) {
    return;
  }

  pending_entity_provider_connections_[agent_url] = {
      std::move(entity_provider_request), std::move(agent_controller_request)};

  MaybeRunAgent(agent_url, [this, agent_url] {
    auto it = pending_entity_provider_connections_.find(agent_url);
    FXL_DCHECK(it != pending_entity_provider_connections_.end());
    running_agents_[agent_url]->NewEntityProviderConnection(
        std::move(it->second.entity_provider_request),
        std::move(it->second.agent_controller_request));
    pending_entity_provider_connections_.erase(it);
  });
}

void AgentRunner::RemoveAgent(const std::string agent_url) {
  running_agents_.erase(agent_url);

  if (*terminating_ && running_agents_.empty()) {
    FXL_DCHECK(termination_callback_);
    termination_callback_();
    return;
  }

  UpdateWatchers();

  // At this point, if there are pending requests to start the agent (because
  // the previous one was in a terminating state), we can start it up again.
  if (run_agent_callbacks_.find(agent_url) != run_agent_callbacks_.end()) {
    RunAgent(agent_url);
  }
}

void AgentRunner::ForwardConnectionsToAgent(const std::string& agent_url) {
  // Did we hold onto new connections as the previous one was exiting?
  auto found_it = pending_agent_connections_.find(agent_url);
  if (found_it != pending_agent_connections_.end()) {
    AgentContextImpl* agent = running_agents_[agent_url].get();
    for (auto& pending_connection : found_it->second) {
      agent->NewAgentConnection(
          pending_connection.requestor_url,
          std::move(pending_connection.incoming_services_request),
          std::move(pending_connection.agent_controller_request));
    }
    pending_agent_connections_.erase(found_it);
  }
}

void AgentRunner::ScheduleTask(const std::string& agent_url,
                               fuchsia::modular::TaskInfo task_info) {
  AgentRunnerStorage::TriggerInfo data;
  data.agent_url = agent_url;
  data.task_id = task_info.task_id.get();

  if (task_info.trigger_condition.is_message_on_queue()) {
    data.queue_name = task_info.trigger_condition.message_on_queue();
    data.task_type = AgentRunnerStorage::TriggerInfo::TYPE_QUEUE_MESSAGE;
  } else if (task_info.trigger_condition.is_queue_deleted()) {
    data.queue_token = task_info.trigger_condition.queue_deleted();
    data.task_type = AgentRunnerStorage::TriggerInfo::TYPE_QUEUE_DELETION;
  } else if (task_info.trigger_condition.is_alarm_in_seconds()) {
    data.task_type = AgentRunnerStorage::TriggerInfo::TYPE_ALARM;
    data.alarm_in_seconds = task_info.trigger_condition.alarm_in_seconds();
  } else {
    // Not a defined trigger condition.
    FXL_NOTREACHED();
  }

  if (task_info.persistent) {
    // |AgentRunnerStorageImpl::WriteTask| eventually calls |AddedTask()| after
    // this trigger information has been added to the ledger via a ledger page
    // watching mechanism.
    agent_runner_storage_->WriteTask(agent_url, data, [](bool) {});
  } else {
    AddedTask(MakeTriggerKey(agent_url, data.task_id), data);
  }
}

void AgentRunner::AddedTask(const std::string& key,
                            AgentRunnerStorage::TriggerInfo data) {
  switch (data.task_type) {
    case AgentRunnerStorage::TriggerInfo::TYPE_QUEUE_MESSAGE:
      ScheduleMessageQueueNewMessageTask(data.agent_url, data.task_id,
                                         data.queue_name);
      break;
    case AgentRunnerStorage::TriggerInfo::TYPE_QUEUE_DELETION:
      ScheduleMessageQueueDeletionTask(data.agent_url, data.task_id,
                                       data.queue_token);
      break;
    case AgentRunnerStorage::TriggerInfo::TYPE_ALARM:
      ScheduleAlarmTask(data.agent_url, data.task_id, data.alarm_in_seconds,
                        true);
      break;
  }

  task_by_ledger_key_[key] = std::make_pair(data.agent_url, data.task_id);
  UpdateWatchers();
}

void AgentRunner::DeletedTask(const std::string& key) {
  auto data = task_by_ledger_key_.find(key);
  if (data == task_by_ledger_key_.end()) {
    // Never scheduled, nothing to delete.
    return;
  }

  DeleteMessageQueueTask(data->second.first, data->second.second);
  DeleteAlarmTask(data->second.first, data->second.second);

  task_by_ledger_key_.erase(key);
  UpdateWatchers();
}

void AgentRunner::DeleteMessageQueueTask(const std::string& agent_url,
                                         const std::string& task_id) {
  auto agent_it = watched_queues_.find(agent_url);
  if (agent_it == watched_queues_.end()) {
    return;
  }

  auto& agent_map = agent_it->second;
  auto task_id_it = agent_map.find(task_id);
  if (task_id_it == agent_map.end()) {
    return;
  }

  // The specific type of message queue task identified by |task_id| is not
  // available, so explicitly clean up both types.
  message_queue_manager_->DropMessageWatcher(kAgentComponentNamespace,
                                             agent_url, task_id_it->second);
  message_queue_manager_->DropDeletionWatcher(kAgentComponentNamespace,
                                              agent_url, task_id_it->second);

  watched_queues_[agent_url].erase(task_id);
  if (watched_queues_[agent_url].empty()) {
    watched_queues_.erase(agent_url);
  }
}

void AgentRunner::DeleteAlarmTask(const std::string& agent_url,
                                  const std::string& task_id) {
  auto agent_it = running_alarms_.find(agent_url);
  if (agent_it == running_alarms_.end()) {
    return;
  }

  auto& agent_map = agent_it->second;
  auto task_id_it = agent_map.find(task_id);
  if (task_id_it == agent_map.end()) {
    return;
  }

  running_alarms_[agent_url].erase(task_id);
  if (running_alarms_[agent_url].empty()) {
    running_alarms_.erase(agent_url);
  }
}

void AgentRunner::ScheduleMessageQueueDeletionTask(
    const std::string& agent_url, const std::string& task_id,
    const std::string& queue_token) {
  auto found_it = watched_queues_.find(agent_url);
  if (found_it != watched_queues_.end()) {
    if (found_it->second.count(task_id) != 0) {
      if (found_it->second[task_id] == queue_token) {
        // This means that we are already watching the message queue.
        // Do nothing.
        return;
      }

      // We were watching some other queue for this task_id. Stop watching.
      message_queue_manager_->DropMessageWatcher(
          kAgentComponentNamespace, agent_url, found_it->second[task_id]);
    }
  } else {
    bool inserted = false;
    std::tie(found_it, inserted) = watched_queues_.emplace(
        agent_url, std::map<std::string, std::string>());
    FXL_DCHECK(inserted);
  }

  found_it->second[task_id] = queue_token;
  message_queue_manager_->RegisterDeletionWatcher(
      kAgentComponentNamespace, agent_url, queue_token,
      [this, agent_url, task_id, terminating = terminating_] {
        // If agent runner is terminating or has already terminated, do not run
        // any new tasks.
        if (*terminating) {
          return;
        }

        MaybeRunAgent(agent_url, [agent_url, task_id, this] {
          running_agents_[agent_url]->NewTask(task_id);
        });
      });
}

void AgentRunner::ScheduleMessageQueueNewMessageTask(
    const std::string& agent_url, const std::string& task_id,
    const std::string& queue_name) {
  auto found_it = watched_queues_.find(agent_url);
  if (found_it != watched_queues_.end()) {
    if (found_it->second.count(task_id) != 0) {
      if (found_it->second[task_id] == queue_name) {
        // This means that we are already watching the message queue.
        // Do nothing.
        return;
      }

      // We were watching some other queue for this task_id. Stop watching.
      message_queue_manager_->DropMessageWatcher(
          kAgentComponentNamespace, agent_url, found_it->second[task_id]);
    }
  } else {
    bool inserted = false;
    std::tie(found_it, inserted) = watched_queues_.emplace(
        agent_url, std::map<std::string, std::string>());
    FXL_DCHECK(inserted);
  }

  found_it->second[task_id] = queue_name;
  auto terminating = terminating_;
  message_queue_manager_->RegisterMessageWatcher(
      kAgentComponentNamespace, agent_url, queue_name,
      [this, agent_url, task_id, terminating] {
        // If agent runner is terminating or has already terminated, do not run
        // any new tasks.
        if (*terminating) {
          return;
        }

        MaybeRunAgent(agent_url, [agent_url, task_id, this] {
          running_agents_[agent_url]->NewTask(task_id);
        });
      });
}

void AgentRunner::ScheduleAlarmTask(const std::string& agent_url,
                                    const std::string& task_id,
                                    const uint32_t alarm_in_seconds,
                                    const bool is_new_request) {
  auto found_it = running_alarms_.find(agent_url);
  if (found_it != running_alarms_.end()) {
    if (found_it->second.count(task_id) != 0 && is_new_request) {
      // We are already running a task with the same task_id. We might
      // just have to update the alarm frequency.
      found_it->second[task_id] = alarm_in_seconds;
      return;
    }
  } else {
    bool inserted = false;
    std::tie(found_it, inserted) =
        running_alarms_.emplace(agent_url, std::map<std::string, uint32_t>());
    FXL_DCHECK(inserted);
  }

  found_it->second[task_id] = alarm_in_seconds;
  auto terminating = terminating_;
  async::PostDelayedTask(
      async_get_default(),
      [this, agent_url, task_id, terminating] {
        // If agent runner is terminating, do not run any new tasks.
        if (*terminating) {
          return;
        }

        // Stop the alarm if entry not found.
        auto found_it = running_alarms_.find(agent_url);
        if (found_it == running_alarms_.end()) {
          return;
        }
        if (found_it->second.count(task_id) == 0) {
          return;
        }

        MaybeRunAgent(agent_url, [agent_url, task_id, found_it, this]() {
          running_agents_[agent_url]->NewTask(task_id);
          ScheduleAlarmTask(agent_url, task_id, found_it->second[task_id],
                            false);
        });
      },
      zx::sec(alarm_in_seconds));
}

void AgentRunner::DeleteTask(const std::string& agent_url,
                             const std::string& task_id) {
  // This works for non-persistent tasks too since
  // |AgentRunnerStorageImpl::DeleteTask| handles missing keys in ledger
  // gracefully.
  agent_runner_storage_->DeleteTask(agent_url, task_id, [](bool) {});
}

fidl::VectorPtr<fidl::StringPtr> AgentRunner::GetAllAgents() {
  // A set of all agents that are either running or scheduled to be run.
  std::set<std::string> agents;
  for (auto const& it : running_agents_) {
    agents.insert(it.first);
  }
  for (auto const& it : watched_queues_) {
    agents.insert(it.first);
  }
  for (auto const& it : running_alarms_) {
    agents.insert(it.first);
  }

  fidl::VectorPtr<fidl::StringPtr> agent_urls;
  // Initialize the size to force non-null.
  agent_urls.resize(0);
  for (auto const& it : agents) {
    agent_urls.push_back(it);
  }

  return agent_urls;
}

void AgentRunner::UpdateWatchers() {
  if (*terminating_) {
    return;
  }

  for (auto& watcher : agent_provider_watchers_.ptrs()) {
    (*watcher)->OnUpdate(GetAllAgents());
  }
}

void AgentRunner::Watch(
    fidl::InterfaceHandle<fuchsia::modular::AgentProviderWatcher> watcher) {
  auto ptr = watcher.Bind();
  // 1. Send this watcher the current list of agents.
  ptr->OnUpdate(GetAllAgents());

  // 2. Add this watcher to a set that is updated when a new list of agents is
  // available.
  agent_provider_watchers_.AddInterfacePtr(std::move(ptr));
}

}  // namespace modular
