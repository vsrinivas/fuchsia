// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_RUNNER_AGENT_RUNNER_AGENT_RUNNER_STORAGE_IMPL_H_
#define PERIDOT_BIN_USER_RUNNER_AGENT_RUNNER_AGENT_RUNNER_STORAGE_IMPL_H_

#include <fuchsia/ledger/cpp/fidl.h>

#include "lib/async/cpp/operation.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/user_runner/agent_runner/agent_runner_storage.h"
#include "peridot/lib/ledger_client/ledger_client.h"
#include "peridot/lib/ledger_client/page_client.h"
#include "peridot/lib/ledger_client/types.h"

namespace modular {

// An implementation of |AgentRunnerStorage| that persists data in the ledger.
class AgentRunnerStorageImpl : public AgentRunnerStorage, PageClient {
 public:
  explicit AgentRunnerStorageImpl(LedgerClient* ledger_client,
                                  fuchsia::ledger::PageId page_id);
  ~AgentRunnerStorageImpl() override;

 private:
  // |AgentRunnerStorage|
  void Initialize(NotificationDelegate* delegate,
                  std::function<void()> done) override;

  // |AgentRunnerStorage|
  void WriteTask(const std::string& agent_url, TriggerInfo data,
                 std::function<void(bool)> done) override;

  // |AgentRunnerStorage|
  void DeleteTask(const std::string& agent_url, const std::string& task_id,
                  std::function<void(bool)> done) override;

  // Operation subclasses:
  class InitializeCall;
  class WriteTaskCall;
  class DeleteTaskCall;

  // |PageClient|
  void OnPageChange(const std::string& key, const std::string& value) override;
  // |PageClient|
  void OnPageDelete(const std::string& key) override;

  // Only valid after |Initialize()| is called.
  NotificationDelegate* delegate_;  // Not owned.

  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AgentRunnerStorageImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_USER_RUNNER_AGENT_RUNNER_AGENT_RUNNER_STORAGE_IMPL_H_
