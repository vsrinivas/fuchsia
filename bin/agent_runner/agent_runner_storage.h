// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_AGENT_RUNNER_AGENT_RUNNER_STORAGE_H_
#define PERIDOT_BIN_AGENT_RUNNER_AGENT_RUNNER_STORAGE_H_

#include <functional>
#include <string>

#include "lib/fxl/macros.h"

namespace modular {

// This abstract class is used by AgentRunner to persist data related to
// agents, such as tasks and their triggers. See |AgentRunnerStorageImpl| for
// an implementation of an AgentRunnerStorage.
class AgentRunnerStorage {
 public:
  AgentRunnerStorage();
  virtual ~AgentRunnerStorage();

  struct TriggerInfo {
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

  // Consumers of AgentRunnerStorage provide a NotificationDelegate
  // implementation to |Initialize()| to receive notifications for newly added
  // and deleted tasks.
  class NotificationDelegate {
   public:
    virtual void AddedTask(const std::string& key,
                           TriggerInfo trigger_info) = 0;
    virtual void DeletedTask(const std::string& key) = 0;
  };

  // Loads up all tasks (across all agents) from storage. |NotifcationDelegate|
  // is notified of each added task, and also for any added and deleted tasks in
  // the future.
  virtual void Initialize(NotificationDelegate* delegate,
                          std::function<void()> done) = 0;

  // Writes a new task to storage. |NotificationDelegate| will be notified of
  // the new task.
  virtual void WriteTask(const std::string& agent_url,
                         TriggerInfo info,
                         std::function<void(bool)> done) = 0;

  // Deletes existing task on the storage. |NotificationDelegate| will be
  // notified of the deleted task.
  virtual void DeleteTask(const std::string& agent_url,
                          const std::string& task_id,
                          std::function<void(bool)> done) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(AgentRunnerStorage);
};

}  // namespace modular

#endif  // PERIDOT_BIN_AGENT_RUNNER_AGENT_RUNNER_STORAGE_H_
