// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/agent_runner/agent_runner.h"

#include "application/lib/app/connect.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/rapidjson/rapidjson.h"
#include "apps/modular/src/agent_runner/agent_context_impl.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/vmo/strings.h"

namespace modular {

namespace {

constexpr char kTriggerListLedger[] = "message_queue_trigger_registry";

constexpr char kTriggerEntryUrl[] = "url";
constexpr char kTriggerEntryTaskId[] = "task_id";
constexpr char kTriggerEntryTaskType[] = "task_type";
constexpr char kTriggerEntryMessageQueue[] = "message_queue";
constexpr char kTriggerEntryQueueName[] = "queue_name";
constexpr char kTriggerEntryAlarm[] = "alarm";
constexpr char kTriggerEntryAlarmSeconds[] = "alarm_in_seconds";

rapidjson::Document ParseTriggerListKey(const std::string& key) {
  rapidjson::Document key_doc;
  key_doc.Parse(key);
  FTL_DCHECK(key_doc.HasMember(kTriggerEntryUrl));
  FTL_DCHECK(key_doc[kTriggerEntryUrl].IsString());
  FTL_DCHECK(key_doc.HasMember(kTriggerEntryTaskId));
  FTL_DCHECK(key_doc[kTriggerEntryTaskId].IsString());
  return key_doc;
}

rapidjson::Document ParseTriggerListValue(const std::string& value) {
  rapidjson::Document value_doc;
  value_doc.Parse(value);
  FTL_DCHECK(value_doc.HasMember(kTriggerEntryTaskType));
  FTL_DCHECK(value_doc[kTriggerEntryTaskType].IsString());
  if (value_doc[kTriggerEntryTaskType] == kTriggerEntryMessageQueue) {
    FTL_DCHECK(value_doc.HasMember(kTriggerEntryQueueName));
    FTL_DCHECK(value_doc[kTriggerEntryQueueName].IsString());
  } else if (value_doc[kTriggerEntryTaskType] == kTriggerEntryAlarm) {
    FTL_DCHECK(value_doc.HasMember(kTriggerEntryAlarmSeconds));
    FTL_DCHECK(value_doc[kTriggerEntryAlarmSeconds].IsUint());
  } else {
    // There are only 2 trigger conditions.
    FTL_NOTREACHED();
  }

  return value_doc;
}

rapidjson::Document CreateTriggerListKey(const std::string& agent_url,
                                         const std::string& task_id) {
  rapidjson::Document key_doc;
  auto& allocator = key_doc.GetAllocator();
  key_doc.SetObject();
  key_doc.AddMember(kTriggerEntryUrl, rapidjson::Value(agent_url, allocator),
                    allocator);
  key_doc.AddMember(kTriggerEntryTaskId, rapidjson::Value(task_id, allocator),
                    allocator);
  return key_doc;
}

}  // namespace

class AgentRunner::PageWatcherImpl : ledger::PageWatcher {
 public:
  PageWatcherImpl(
      ledger::LedgerRepository* const ledger_repository,
      const std::string& ledger_name,
      std::function<void(const std::string&, const std::string&)> added_entry,
      std::function<void(const std::string&)> deleted_entry)
      : binding_(this),
        added_entry_(added_entry),
        deleted_entry_(deleted_entry) {
    auto error_handler = [](ledger::Status status) {
      if (status != ledger::Status::OK) {
        FTL_LOG(ERROR) << "Ledger operation returned status: " << status;
      }
    };

    ledger_repository->GetLedger(to_array(ledger_name), ledger_.NewRequest(),
                                 error_handler);
    ledger_->GetRootPage(page_.NewRequest(), error_handler);

    page_->GetSnapshot(snapshot_.NewRequest(), binding_.NewBinding(),
                       error_handler);

    snapshot_->GetEntries(
        nullptr, nullptr,
        [this](ledger::Status status, fidl::Array<ledger::EntryPtr> entries,
               fidl::Array<uint8_t> continuation_token) {
          // TODO(alhaad): It is possible that entries in ledger snapshot are
          // played in multiple runs. Handle it!
          if (status != ledger::Status::OK) {
            FTL_LOG(ERROR) << "Ledger status " << status << "."
                           << "This maybe because of the TODO above";
            return;
          }

          if (entries.size() == 0) {
            // No existing entries.
            return;
          }

          for (const auto& entry : entries) {
            std::string key(reinterpret_cast<const char*>(entry->key.data()),
                            entry->key.size());
            std::string value;
            if (!mtl::StringFromVmo(entry->value, &value)) {
              FTL_LOG(ERROR) << "VMO for key " << key << " couldn't be copied.";
              return;
            }
            added_entry_(key, value);
          }

          // We don't use snapshot_ anymore.
          snapshot_.reset();
        });
  }

  void Add(const std::string& key, const std::string& value) {
    page_->PutWithPriority(
        to_array(key), to_array(value), ledger::Priority::EAGER,
        [this](ledger::Status status) {
          if (status != ledger::Status::OK) {
            FTL_LOG(ERROR) << "Ledger operation returned status: " << status;
          }
        });
  }

  void Remove(const std::string& key) {
    page_->Delete(to_array(key), [this](ledger::Status status) {
      // ledger::Status::INVALID_TOKEN is okay because we might have gotten a
      // request to delete a token which does not exist. This is okay.
      if (status != ledger::Status::OK &&
          status != ledger::Status::INVALID_TOKEN) {
        FTL_LOG(ERROR) << "Ledger operation returned status: " << status;
      }
    });
  }

 private:
  void OnChange(ledger::PageChangePtr page,
                ledger::ResultState result_state,
                const OnChangeCallback& callback) override {
    for (auto& entry : page->changes) {
      std::string key(reinterpret_cast<const char*>(entry->key.data()),
                      entry->key.size());
      std::string value;
      if (!mtl::StringFromVmo(entry->value, &value)) {
        FTL_LOG(ERROR) << "VMO for key " << key << " couldn't be copied.";
        return;
      }
      added_entry_(key, value);
    }

    for (auto& key : page->deleted_keys) {
      deleted_entry_(to_string(key));
    }
    callback(nullptr);
  }

  fidl::Binding<ledger::PageWatcher> binding_;
  ledger::LedgerPtr ledger_;
  ledger::PagePtr page_;
  ledger::PageSnapshotPtr snapshot_;

  std::function<void(const std::string&, const std::string&)> const
      added_entry_;
  std::function<void(const std::string&)> const deleted_entry_;

  FTL_DISALLOW_COPY_AND_ASSIGN(PageWatcherImpl);
};

AgentRunner::AgentRunner(
    app::ApplicationLauncher* const application_launcher,
    MessageQueueManager* const message_queue_manager,
    ledger::LedgerRepository* const ledger_repository,
    maxwell::UserIntelligenceProvider* const user_intelligence_provider)
    : application_launcher_(application_launcher),
      message_queue_manager_(message_queue_manager),
      ledger_repository_(ledger_repository),
      user_intelligence_provider_(user_intelligence_provider) {
  trigger_list_watcher_.reset(new PageWatcherImpl(
      ledger_repository, kTriggerListLedger,
      [this](const std::string& key, const std::string& value) {
        AddedTrigger(key, value);
      },
      [this](const std::string& key) { DeletedTrigger(key); }));
}

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
                               TaskInfoPtr task_info) {
  auto key_doc = CreateTriggerListKey(agent_url, task_info->task_id);

  rapidjson::Document value_doc;
  auto& allocator2 = key_doc.GetAllocator();
  value_doc.SetObject();
  if (task_info->trigger_condition->is_queue_name()) {
    value_doc.AddMember(kTriggerEntryTaskType,
                        rapidjson::Value(kTriggerEntryMessageQueue, allocator2),
                        allocator2);
    value_doc.AddMember(
        kTriggerEntryQueueName,
        rapidjson::Value(task_info->trigger_condition->get_queue_name(),
                         allocator2),
        allocator2);
  } else if (task_info->trigger_condition->is_alarm_in_seconds()) {
    value_doc.AddMember(kTriggerEntryTaskType,
                        rapidjson::Value(kTriggerEntryAlarm, allocator2),
                        allocator2);
    value_doc.AddMember(
        kTriggerEntryAlarmSeconds,
        rapidjson::Value().SetUint(
            task_info->trigger_condition->get_alarm_in_seconds()),
        allocator2);
  } else {
    // Not a defined trigger condition.
    FTL_NOTREACHED();
  }

  trigger_list_watcher_->Add(modular::JsonValueToString(key_doc),
                             modular::JsonValueToString(value_doc));
}

void AgentRunner::AddedTrigger(const std::string& key,
                               const std::string& value) {
  auto key_doc = ParseTriggerListKey(key);
  auto value_doc = ParseTriggerListValue(value);

  if (value_doc[kTriggerEntryTaskType] == kTriggerEntryMessageQueue) {
    ScheduleMessageQueueTask(key_doc[kTriggerEntryUrl].GetString(),
                             key_doc[kTriggerEntryTaskId].GetString(),
                             value_doc[kTriggerEntryQueueName].GetString());
  } else if (value_doc[kTriggerEntryTaskType] == kTriggerEntryAlarm) {
    ScheduleAlarmTask(key_doc[kTriggerEntryUrl].GetString(),
                      key_doc[kTriggerEntryTaskId].GetString(),
                      value_doc[kTriggerEntryAlarmSeconds].GetUint(), true);
  } else {
    // Not a defined trigger condition.
    FTL_NOTREACHED();
  }
}

void AgentRunner::DeletedTrigger(const std::string& key) {
  auto key_doc = ParseTriggerListKey(key);
  DeleteMessageQueueTask(key_doc[kTriggerEntryUrl].GetString(),
                         key_doc[kTriggerEntryTaskId].GetString());
  DeleteAlarmTask(key_doc[kTriggerEntryUrl].GetString(),
                  key_doc[kTriggerEntryTaskId].GetString());
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

  message_queue_manager_->DropWatcher(agent_url, task_id_it->second);
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
      // This means that we are already watching the message queue.
      if (found_it->second[task_id] == queue_name) {
        // We are already watching this queue. Do nothing.
        return;
      }
      // We were watching some other queue for this task_id. Stop watching.
      message_queue_manager_->DropWatcher(agent_url, found_it->second[task_id]);
    }
  } else {
    bool inserted = false;
    std::tie(found_it, inserted) = watched_queues_.emplace(
        agent_url, std::unordered_map<std::string, std::string>());
    FTL_DCHECK(inserted);
  }

  found_it->second[task_id] = queue_name;
  message_queue_manager_->RegisterWatcher(
      agent_url, queue_name, [this, agent_url, task_id] {
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
      // We are already running a task with the same task_id. We might just have
      // to update the alarm frequency.
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
  mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
      [this, agent_url, task_id] {
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
  const auto& key =
      modular::JsonValueToString(CreateTriggerListKey(agent_url, task_id));
  trigger_list_watcher_->Remove(key);
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
  }

  return found_it->second.get();
}

}  // namespace modular
