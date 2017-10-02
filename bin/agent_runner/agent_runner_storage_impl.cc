// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/agent_runner/agent_runner_storage_impl.h"

#include <functional>
#include <utility>

#include "lib/fsl/vmo/strings.h"
#include "lib/ledger/fidl/ledger.fidl.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/fidl/json_xdr.h"
#include "peridot/lib/fidl/operation.h"
#include "peridot/lib/ledger_client/storage.h"

namespace modular {
namespace {

void XdrTriggerInfo(XdrContext* const xdr,
                    AgentRunnerStorage::TriggerInfo* const data) {
  xdr->Field("agent_url", &data->agent_url);
  xdr->Field("task_id", &data->task_id);
  xdr->Field("task_type", &data->task_type);

  switch (data->task_type) {
    case AgentRunnerStorage::TriggerInfo::TYPE_ALARM:
      xdr->Field("alarm_in_seconds", &data->alarm_in_seconds);
      break;
    case AgentRunnerStorage::TriggerInfo::TYPE_QUEUE:
      xdr->Field("queue_name", &data->queue_name);
      break;
  }
}

}  // namespace

class AgentRunnerStorageImpl::InitializeCall : Operation<> {
 public:
  InitializeCall(OperationContainer* const container,
                 NotificationDelegate* const delegate,
                 std::shared_ptr<ledger::PageSnapshotPtr> const snapshot,
                 std::function<void()> done)
      : Operation("AgentRunnerStorageImpl::InitializeCall",
                  container,
                  std::move(done)),
        delegate_(delegate),
        snapshot_(snapshot) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};

    GetEntries((*snapshot_).get(), &entries_,
               [this, flow](ledger::Status status) {
                 if (status != ledger::Status::OK) {
                   FXL_LOG(ERROR) << "InitializeCall() "
                                  << "GetEntries() " << status;
                   return;
                 }

                 Cont(flow);
               });
  }

  void Cont(FlowToken /*flow*/) {
    if (entries_.empty()) {
      // No existing entries.
      return;
    }

    for (const auto& entry : entries_) {
      std::string key(reinterpret_cast<const char*>(entry->key.data()),
                      entry->key.size());
      std::string value;
      if (!fsl::StringFromVmo(entry->value, &value)) {
        FXL_LOG(ERROR) << "VMO for key " << key << " couldn't be copied.";
        continue;
      }

      TriggerInfo data;
      if (!XdrRead(value, &data, XdrTriggerInfo)) {
        return;
      }
      delegate_->AddedTask(key, std::move(data));
    }
  }

  NotificationDelegate* const delegate_;
  std::shared_ptr<ledger::PageSnapshotPtr> snapshot_;
  std::vector<ledger::EntryPtr> entries_;
  FXL_DISALLOW_COPY_AND_ASSIGN(InitializeCall);
};

class AgentRunnerStorageImpl::WriteTaskCall : Operation<bool> {
 public:
  WriteTaskCall(OperationContainer* const container,
                AgentRunnerStorageImpl* storage,
                std::string agent_url,
                TriggerInfo data,
                std::function<void(bool)> done)
      : Operation("AgentRunnerStorageImpl::WriteTaskCall", container, done),
        storage_(storage),
        agent_url_(std::move(agent_url)),
        data_(std::move(data)) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this, &success_result_};

    std::string key = MakeTriggerKey(agent_url_, data_.task_id);
    std::string value;
    XdrWrite(&value, &data_, XdrTriggerInfo);

    storage_->page()->PutWithPriority(
        to_array(key), to_array(value), ledger::Priority::EAGER,
        [this, flow](ledger::Status status) {
          if (status != ledger::Status::OK) {
            FXL_LOG(ERROR) << "Ledger operation returned status: " << status;
            return;
          }

          success_result_ = true;
        });
  }

  bool success_result_ = false;
  AgentRunnerStorageImpl* const storage_;
  const std::string agent_url_;
  TriggerInfo data_;

  FXL_DISALLOW_COPY_AND_ASSIGN(WriteTaskCall);
};

class AgentRunnerStorageImpl::DeleteTaskCall : Operation<bool> {
 public:
  DeleteTaskCall(OperationContainer* const container,
                 AgentRunnerStorageImpl* storage,
                 std::string agent_url,
                 std::string task_id,
                 std::function<void(bool)> done)
      : Operation("AgentRunnerStorageImpl::DeleteTaskCall", container, done),
        storage_(storage),
        agent_url_(std::move(agent_url)),
        task_id_(std::move(task_id)) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this, &success_result_};

    std::string key = MakeTriggerKey(agent_url_, task_id_);
    storage_->page()->Delete(
        to_array(key), [this, flow](ledger::Status status) {
          // ledger::Status::INVALID_TOKEN is okay because we might have gotten
          // a request to delete a token which does not exist. This is okay.
          if (status != ledger::Status::OK &&
              status != ledger::Status::INVALID_TOKEN) {
            FXL_LOG(ERROR) << "Ledger operation returned status: " << status;
            return;
          }
          success_result_ = true;
        });
  }

  bool success_result_ = false;
  AgentRunnerStorageImpl* const storage_;
  const std::string agent_url_;
  const std::string task_id_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DeleteTaskCall);
};

AgentRunnerStorageImpl::AgentRunnerStorageImpl(LedgerClient* ledger_client,
                                               LedgerPageId page_id)
    : PageClient("AgentRunnerStorageImpl", ledger_client, std::move(page_id)) {}

AgentRunnerStorageImpl::~AgentRunnerStorageImpl() = default;

void AgentRunnerStorageImpl::Initialize(NotificationDelegate* const delegate,
                                        std::function<void()> done) {
  FXL_DCHECK(!delegate_);
  delegate_ = delegate;
  new InitializeCall(&operation_queue_, delegate_, page_snapshot(),
                     std::move(done));
}

void AgentRunnerStorageImpl::WriteTask(const std::string& agent_url,
                                       const TriggerInfo data,
                                       std::function<void(bool)> done) {
  new WriteTaskCall(&operation_queue_, this, agent_url, data, std::move(done));
}

void AgentRunnerStorageImpl::DeleteTask(const std::string& agent_url,
                                        const std::string& task_id,
                                        std::function<void(bool)> done) {
  new DeleteTaskCall(&operation_queue_, this, agent_url, task_id,
                     std::move(done));
}

void AgentRunnerStorageImpl::OnPageChange(const std::string& key,
                                          const std::string& value) {
  FXL_DCHECK(delegate_ != nullptr);
  new SyncCall(&operation_queue_, [this, key, value] {
    TriggerInfo data;
    if (!XdrRead(value, &data, XdrTriggerInfo)) {
      return;
    }
    delegate_->AddedTask(key, data);
  });
}

void AgentRunnerStorageImpl::OnPageDelete(const std::string& key) {
  FXL_DCHECK(delegate_ != nullptr);
  new SyncCall(&operation_queue_, [this, key] { delegate_->DeletedTask(key); });
}

}  // namespace modular
