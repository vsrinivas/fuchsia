// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/agent_runner/agent_runner.h"

#include <mutex>
#include <unordered_set>

#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/fidl/json_xdr.h"
#include "apps/modular/lib/ledger/storage.h"
#include "apps/modular/src/agent_runner/agent_context_impl.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/vmo/strings.h"

namespace modular {

constexpr ftl::TimeDelta kTeardownTimeout = ftl::TimeDelta::FromSeconds(3);

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

class AgentRunner::InitializeCall : Operation<> {
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
  std::string GetName() const override { return "AgentRunner::InitializeCall"; }

  void Run() override {
    FlowToken flow{this};

    GetEntries((*snapshot_).get(), &entries_,
               [this, flow](ledger::Status status) {
                 if (status != ledger::Status::OK) {
                   FTL_LOG(ERROR) << "InitializeCall() "
                                  << "GetEntries() " << status;
                   return;
                 }

                 Cont(flow);
               });
  }

  void Cont(FlowToken flow) {
    if (entries_.size() == 0) {
      // No existing entries.
      return;
    }

    for (const auto& entry : entries_) {
      std::string key(reinterpret_cast<const char*>(entry->key.data()),
                      entry->key.size());
      std::string value;
      if (!mtl::StringFromVmo(entry->value, &value)) {
        FTL_LOG(ERROR) << "VMO for key " << key << " couldn't be copied.";
        continue;
      }
      agent_runner_->AddedTrigger(key, std::move(value));
    }
  }

  AgentRunner* const agent_runner_;
  std::shared_ptr<ledger::PageSnapshotPtr> snapshot_;
  std::vector<ledger::EntryPtr> entries_;
  FTL_DISALLOW_COPY_AND_ASSIGN(InitializeCall);
};

class AgentRunner::UpdateCall : Operation<> {
 public:
  UpdateCall(OperationContainer* const container,
             AgentRunner* const agent_runner,
             const std::string& key,
             const std::string& value)
      : Operation(container, [] {}),
        agent_runner_(agent_runner),
        key_(key),
        value_(value) {
    Ready();
  }

 private:
  std::string GetName() const override { return "AgentRunner::UpdateCall"; }

  void Run() override {
    agent_runner_->AddedTrigger(key_, value_);
    Done();
  }

  AgentRunner* const agent_runner_;
  const std::string key_;
  const std::string value_;
  FTL_DISALLOW_COPY_AND_ASSIGN(UpdateCall);
};

class AgentRunner::DeleteCall : Operation<> {
 public:
  DeleteCall(OperationContainer* const container,
             AgentRunner* const agent_runner,
             const std::string& key)
      : Operation(container, [] {}), agent_runner_(agent_runner), key_(key) {
    Ready();
  }

 private:
  std::string GetName() const override { return "AgentRunner::DeleteCall"; }

  void Run() override {
    agent_runner_->DeletedTrigger(key_);
    Done();
  }

  AgentRunner* const agent_runner_;
  const std::string key_;
  FTL_DISALLOW_COPY_AND_ASSIGN(DeleteCall);
};

AgentRunner::AgentRunner(
    app::ApplicationLauncher* const application_launcher,
    MessageQueueManager* const message_queue_manager,
    ledger::LedgerRepository* const ledger_repository,
    ledger::PagePtr page,
    fidl::InterfaceHandle<auth::TokenProviderFactory> token_provider_factory,
    maxwell::UserIntelligenceProvider* const user_intelligence_provider)
    : PageClient("AgentRunner", page.get(), nullptr),
      application_launcher_(application_launcher),
      message_queue_manager_(message_queue_manager),
      ledger_repository_(ledger_repository),
      page_(std::move(page)),
      token_provider_factory_(auth::TokenProviderFactoryPtr::Create(
          std::move(token_provider_factory))),
      user_intelligence_provider_(user_intelligence_provider),
      terminating_(std::make_shared<bool>(false)) {
  new InitializeCall(&operation_queue_, this, page_snapshot());
}

AgentRunner::~AgentRunner() = default;

void AgentRunner::Connect(fidl::InterfaceRequest<AgentProvider> request) {
  agent_provider_bindings_.AddBinding(this, std::move(request));
}

void AgentRunner::Teardown(const std::function<void()>& callback) {
  // No new agents will be scheduled to run.
  *terminating_ = true;

  // No agents were running, we are good to go.
  if (running_agents_.empty()) {
    callback();
    return;
  }

  auto once = std::make_unique<std::once_flag>();
  // This is called when agents are done being removed
  termination_callback_ =
      ftl::MakeCopyable([ this, callback, once = std::move(once) ]() {
        std::call_once(*once, callback);
      });

  for (auto& it : running_agents_) {
    // The running agent will call |AgentRunner::RemoveAgent()| to remove itself
    // from the agent runner. When all agents are done being removed,
    // |RemoveAgent()| will call |termination_callback_|.
    it.second->StopForTeardown();
  }

  mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
      termination_callback_, kTeardownTimeout);
}

void AgentRunner::MaybeRunAgent(const std::string& agent_url,
                                const ftl::Closure& done) {
  auto agent_it = running_agents_.find(agent_url);
  if (agent_it != running_agents_.end()) {
    if (agent_it->second->state() == AgentContextImpl::State::TERMINATING) {
      run_agent_callbacks_[agent_url].push_back(done);
      return;
    }
    // Agent is already running, so we can issue the callback immediately.
    done();
    return;
  }

  run_agent_callbacks_[agent_url].push_back(done);

  RunAgent(agent_url);
}

void AgentRunner::RunAgent(const std::string& agent_url) {
  // Start the agent and issue all callbacks.
  ComponentContextInfo component_info = {message_queue_manager_, this,
                                         ledger_repository_};
  AgentContextInfo info = {component_info, application_launcher_,
                           token_provider_factory_.get(),
                           user_intelligence_provider_};

  FTL_CHECK(running_agents_
                .emplace(agent_url,
                         std::make_unique<AgentContextImpl>(info, agent_url))
                .second);

  auto run_callbacks_it = run_agent_callbacks_.find(agent_url);
  if (run_callbacks_it != run_agent_callbacks_.end()) {
    for (auto& callback : run_callbacks_it->second) {
      callback();
    }
    run_agent_callbacks_.erase(agent_url);
  }

  UpdateWatchers();
  ForwardConnectionsToAgent(agent_url);
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

  pending_agent_connections_[agent_url].push_back(
      {requestor_url, std::move(incoming_services_request),
       std::move(agent_controller_request)});

  MaybeRunAgent(agent_url, [this, agent_url] {
    // If the agent was terminating and has restarted, forwarding connections
    // here is redundant, since it was already forwarded earlier.
    ForwardConnectionsToAgent(agent_url);
  });
}

void AgentRunner::RemoveAgent(const std::string agent_url) {
  running_agents_.erase(agent_url);

  if (*terminating_ && running_agents_.empty()) {
    FTL_DCHECK(termination_callback_);
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
      agent->NewConnection(
          pending_connection.requestor_url,
          std::move(pending_connection.incoming_services_request),
          std::move(pending_connection.agent_controller_request));
    }
    pending_agent_connections_.erase(found_it);
  }
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

        MaybeRunAgent(agent_url, [agent_url, task_id, found_it, this]() {
          running_agents_[agent_url]->NewTask(task_id);
          ScheduleAlarmTask(agent_url, task_id, found_it->second[task_id],
                            false);
        });
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

void AgentRunner::UpdateWatchers() {
  if (*terminating_)
    return;

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

void AgentRunner::OnChange(const std::string& key, const std::string& value) {
  new UpdateCall(&operation_queue_, this, key, value);
}

void AgentRunner::OnDelete(const std::string& key) {
  new DeleteCall(&operation_queue_, this, key);
}

}  // namespace modular
