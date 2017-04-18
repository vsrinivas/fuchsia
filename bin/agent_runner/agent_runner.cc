// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/agent_runner/agent_runner.h"

#include <unordered_set>

#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/fidl/json_xdr.h"
#include "apps/modular/lib/ledger/storage.h"
#include "apps/modular/src/agent_runner/agent_context_impl.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/vmo/strings.h"

namespace modular {

struct AgentRunner::TriggerInfo {
  std::string agent_url;
  std::string task_id;

  // NOTE(mesch): We could include the TaskInfo fidl struct here
  // directly, but it contains a union, and dealing with a fidl union
  // in XDR is still rather complicated if we don't want to serialize
  // the union tag enum value directly.
  enum TaskType {
    TYPE_ALARM = 0,
    TYPE_QUEUE = 1,
  };

  TaskType task_type{};

  std::string queue_name;
  uint32_t alarm_in_seconds{};
};

void AgentRunner::XdrTriggerInfo(XdrContext* const xdr,
                                 TriggerInfo* const data) {
  xdr->Field("agent_url", &data->agent_url);
  xdr->Field("task_id", &data->task_id);
  xdr->Field("task_type", &data->task_type);

  switch (data->task_type) {
    case TriggerInfo::TYPE_ALARM:
      xdr->Field("alarm_in_seconds", &data->alarm_in_seconds);
      break;
    case TriggerInfo::TYPE_QUEUE:
      xdr->Field("queue_name", &data->queue_name);
      break;
  }
}

// Asynchronous operations of this service.

class AgentRunner::InitializeCall : Operation<void> {
 public:
  InitializeCall(OperationContainer* const container,
                 AgentRunner* const agent_runner,
                 std::shared_ptr<ledger::PageSnapshotPtr> const snapshot)
      : Operation(container, [] {}),
        agent_runner_(agent_runner),
        snapshot_(snapshot) {
    Ready();
  }

 private:
  void Run() override { GetEntries(nullptr); }

  void GetEntries(fidl::Array<uint8_t> continuation_token) {
    (*snapshot_)
        ->GetEntries(
            nullptr, std::move(continuation_token),
            [this](ledger::Status status, fidl::Array<ledger::EntryPtr> entries,
                   fidl::Array<uint8_t> continuation_token) {
              if (status != ledger::Status::OK &&
                  status != ledger::Status::PARTIAL_RESULT) {
                FTL_LOG(ERROR) << "Ledger status " << status << ".";
                Done();
                return;
              }

              if (entries.size() == 0) {
                // No existing entries.
                Done();
                return;
              }
              for (const auto& entry : entries) {
                std::string key(
                    reinterpret_cast<const char*>(entry->key.data()),
                    entry->key.size());
                std::string value;
                if (!mtl::StringFromVmo(entry->value, &value)) {
                  FTL_LOG(ERROR)
                      << "VMO for key " << key << " couldn't be copied.";
                  return;
                }
                agent_runner_->AddedTrigger(key, std::move(value));
              }

              if (status == ledger::Status::PARTIAL_RESULT) {
                GetEntries(std::move(continuation_token));
              } else {
                Done();
              }
            });
  }

  AgentRunner* const agent_runner_;
  std::shared_ptr<ledger::PageSnapshotPtr> snapshot_;
  FTL_DISALLOW_COPY_AND_ASSIGN(InitializeCall);
};

class AgentRunner::UpdateCall : Operation<void> {
 public:
  UpdateCall(OperationContainer* const container,
             AgentRunner* const agent_runner,
             ledger::PageChangePtr page,
             ResultCall result_call)
      : Operation(container, std::move(result_call)),
        agent_runner_(agent_runner),
        page_(std::move(page)) {
    Ready();
  }

 private:
  void Run() override {
    for (auto& entry : page_->changes) {
      std::string key(reinterpret_cast<const char*>(entry->key.data()),
                      entry->key.size());
      std::string value;
      if (!mtl::StringFromVmo(entry->value, &value)) {
        FTL_LOG(ERROR) << "VMO for key " << key << " couldn't be copied.";
        continue;
      }
      agent_runner_->AddedTrigger(key, value);
    }

    for (auto& key : page_->deleted_keys) {
      agent_runner_->DeletedTrigger(to_string(key));
    }
  }

  AgentRunner* const agent_runner_;
  ledger::PageChangePtr page_;
  FTL_DISALLOW_COPY_AND_ASSIGN(UpdateCall);
};

AgentRunner::AgentRunner(
    app::ApplicationLauncher* const application_launcher,
    MessageQueueManager* const message_queue_manager,
    ledger::LedgerRepository* const ledger_repository,
    ledger::PagePtr page,
    maxwell::UserIntelligenceProvider* const user_intelligence_provider)
    : application_launcher_(application_launcher),
      message_queue_manager_(message_queue_manager),
      ledger_repository_(ledger_repository),
      page_(std::move(page)),
      user_intelligence_provider_(user_intelligence_provider),
      watcher_binding_(this),
      page_client_("AgentRunner"),
      terminating_(std::make_shared<bool>(false)) {
  page_->GetSnapshot(page_client_.NewRequest(), watcher_binding_.NewBinding(),
                     [](ledger::Status status) {
                       if (status != ledger::Status::OK) {
                         FTL_LOG(ERROR)
                             << "Ledger operation returned status: " << status;
                       }
                     });
  new InitializeCall(&operation_queue_, this, page_client_.page_snapshot());
}

AgentRunner::~AgentRunner() = default;

void AgentRunner::Connect(fidl::InterfaceRequest<AgentProvider> request) {
  agent_provider_bindings_.AddBinding(this, std::move(request));
}

void AgentRunner::Teardown(const std::function<void()>& callback) {
  // No new agents will be scheduled to run.
  *terminating_ = true;

  // No agents were running, we are good to go.
  if (running_agents_.size() == 0) {
    callback();
    return;
  }

  auto cont = [this, callback] {
    if (running_agents_.size() == 0) {
      callback();
    }
  };

  for (auto& it : running_agents_) {
    it.second->StopForTeardown(cont);
  }
}

void AgentRunner::ConnectToAgent(
    const std::string& requestor_url,
    const std::string& agent_url,
    fidl::InterfaceRequest<app::ServiceProvider> incoming_services_request,
    fidl::InterfaceRequest<AgentController> agent_controller_request) {
  // Drop all new requests if AgentRunner is terminating.
  if (*terminating_) {
    return;
  }

  MaybeRunAgent(agent_url)->NewConnection(requestor_url,
                                          std::move(incoming_services_request),
                                          std::move(agent_controller_request));
}

void AgentRunner::RemoveAgent(const std::string& agent_url) {
  running_agents_.erase(agent_url);
  UpdateWatchers();
}

void AgentRunner::ScheduleTask(const std::string& agent_url,
                               TaskInfoPtr task_info) {
  std::string key = MakeTriggerKey(agent_url, task_info->task_id);

  TriggerInfo data;
  data.agent_url = agent_url;
  data.task_id = task_info->task_id.get();

  if (task_info->trigger_condition->is_queue_name()) {
    data.task_type = TriggerInfo::TYPE_QUEUE;
    data.queue_name = task_info->trigger_condition->get_queue_name().get();

  } else if (task_info->trigger_condition->is_alarm_in_seconds()) {
    data.task_type = TriggerInfo::TYPE_ALARM;
    data.alarm_in_seconds =
        task_info->trigger_condition->get_alarm_in_seconds();

  } else {
    // Not a defined trigger condition.
    FTL_NOTREACHED();
  }

  std::string value;
  XdrWrite(&value, &data, XdrTriggerInfo);

  page_->PutWithPriority(
      to_array(key), to_array(value), ledger::Priority::EAGER,
      [this](ledger::Status status) {
        if (status != ledger::Status::OK) {
          FTL_LOG(ERROR) << "Ledger operation returned status: " << status;
        }
      });
}

void AgentRunner::AddedTrigger(const std::string& key, std::string value) {
  TriggerInfo data;
  if (!XdrRead(value, &data, XdrTriggerInfo)) {
    return;
  }

  switch (data.task_type) {
    case TriggerInfo::TYPE_QUEUE:
      ScheduleMessageQueueTask(data.agent_url, data.task_id, data.queue_name);
      break;
    case TriggerInfo::TYPE_ALARM:
      ScheduleAlarmTask(data.agent_url, data.task_id, data.alarm_in_seconds,
                        true);
      break;
  }

  task_by_ledger_key_[key] = std::make_pair(data.agent_url, data.task_id);
  UpdateWatchers();
}

void AgentRunner::DeletedTrigger(const std::string& key) {
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

  message_queue_manager_->DropWatcher(kAgentComponentNamespace, agent_url,
                                      task_id_it->second);
  watched_queues_[agent_url].erase(task_id);
  if (watched_queues_[agent_url].size() == 0) {
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
  if (running_alarms_[agent_url].size() == 0) {
    running_alarms_.erase(agent_url);
  }
}

void AgentRunner::ScheduleMessageQueueTask(const std::string& agent_url,
                                           const std::string& task_id,
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
      message_queue_manager_->DropWatcher(kAgentComponentNamespace, agent_url,
                                          found_it->second[task_id]);
    }

  } else {
    bool inserted = false;
    std::tie(found_it, inserted) = watched_queues_.emplace(
        agent_url, std::unordered_map<std::string, std::string>());
    FTL_DCHECK(inserted);
  }

  found_it->second[task_id] = queue_name;
  auto terminating = terminating_;
  message_queue_manager_->RegisterWatcher(
      kAgentComponentNamespace, agent_url, queue_name,
      [this, agent_url, task_id, terminating] {
        // If agent runner is terminating, do not run any new tasks.
        if (*terminating) {
          return;
        }

        MaybeRunAgent(agent_url)->NewTask(task_id);
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
    std::tie(found_it, inserted) = running_alarms_.emplace(
        agent_url, std::unordered_map<std::string, uint32_t>());
    FTL_DCHECK(inserted);
  }

  found_it->second[task_id] = alarm_in_seconds;
  auto terminating = terminating_;
  mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
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

        MaybeRunAgent(agent_url)->NewTask(task_id);
        ScheduleAlarmTask(agent_url, task_id, found_it->second[task_id], false);
      },
      ftl::TimeDelta::FromSeconds(alarm_in_seconds));
}

void AgentRunner::DeleteTask(const std::string& agent_url,
                             const std::string& task_id) {
  std::string key = MakeTriggerKey(agent_url, task_id);
  page_->Delete(to_array(key), [this](ledger::Status status) {
    // ledger::Status::INVALID_TOKEN is okay because we might have gotten a
    // request to delete a token which does not exist. This is okay.
    if (status != ledger::Status::OK &&
        status != ledger::Status::INVALID_TOKEN) {
      FTL_LOG(ERROR) << "Ledger operation returned status: " << status;
    }
  });
}

AgentContextImpl* AgentRunner::MaybeRunAgent(const std::string& agent_url) {
  auto found_it = running_agents_.find(agent_url);
  if (found_it == running_agents_.end()) {
    bool inserted = false;
    ComponentContextInfo component_info = {message_queue_manager_, this,
                                           ledger_repository_};
    AgentContextInfo info = {component_info, application_launcher_,
                             user_intelligence_provider_};
    std::tie(found_it, inserted) = running_agents_.emplace(
        agent_url, std::make_unique<AgentContextImpl>(info, agent_url));
    FTL_DCHECK(inserted);

    UpdateWatchers();
  }

  return found_it->second.get();
}

void AgentRunner::UpdateWatchers() {
  // A set of all agents that are either running or scheduled to be run.
  std::unordered_set<std::string> agents;
  for (auto const& it : running_agents_) {
    agents.insert(it.first);
  }
  for (auto const& it : watched_queues_) {
    agents.insert(it.first);
  }
  for (auto const& it : running_alarms_) {
    agents.insert(it.first);
  }

  fidl::Array<fidl::String> agent_urls;
  for (auto const& it : agents) {
    agent_urls.push_back(it);
  }

  agent_provider_watchers_.ForAllPtrs([agent_urls = std::move(agent_urls)](
      AgentProviderWatcher * watcher) {
    watcher->OnUpdate(agent_urls.Clone());
  });
}

void AgentRunner::Watch(fidl::InterfaceHandle<AgentProviderWatcher> watcher) {
  agent_provider_watchers_.AddInterfacePtr(
      AgentProviderWatcherPtr::Create(std::move(watcher)));
}

void AgentRunner::OnChange(ledger::PageChangePtr page,
                           ledger::ResultState result_state,
                           const OnChangeCallback& callback) {
  new UpdateCall(&operation_queue_, this, std::move(page),
                 [callback] { callback(nullptr); });
}

}  // namespace modular
