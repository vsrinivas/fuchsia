// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_AGENT_RUNNER_AGENT_RUNNER_STORAGE_IMPL_H_
#define APPS_MODULAR_SRC_AGENT_RUNNER_AGENT_RUNNER_STORAGE_IMPL_H_

#include "apps/ledger/services/public/ledger.fidl.h"
#include "peridot/lib/fidl/operation.h"
#include "peridot/lib/ledger/ledger_client.h"
#include "peridot/lib/ledger/page_client.h"
#include "peridot/lib/ledger/types.h"
#include "peridot/bin/agent_runner/agent_runner_storage.h"
#include "lib/fxl/macros.h"

namespace modular {

// An implementation of |AgentRunnerStorage| that persists data in the ledger.
class AgentRunnerStorageImpl : public AgentRunnerStorage, PageClient {
 public:
  explicit AgentRunnerStorageImpl(LedgerClient* ledger_client, LedgerPageId page_id);
  ~AgentRunnerStorageImpl() override;

 private:
  // |AgentRunnerStorage|
  void Initialize(NotificationDelegate* delegate,
                  std::function<void()> done) override;

  // |AgentRunnerStorage|
  void WriteTask(const std::string& agent_url,
                 TriggerInfo data,
                 std::function<void(bool)> done) override;

  // |AgentRunnerStorage|
  void DeleteTask(const std::string& agent_url,
                  const std::string& task_id,
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
  NotificationDelegate* delegate_;

  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AgentRunnerStorageImpl);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_AGENT_RUNNER_AGENT_RUNNER_STORAGE_IMPL_H_
