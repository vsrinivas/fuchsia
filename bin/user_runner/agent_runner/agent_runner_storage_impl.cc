// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/agent_runner/agent_runner_storage_impl.h"

#include <functional>
#include <utility>

#include <fuchsia/ledger/cpp/fidl.h>

#include "lib/async/cpp/operation.h"
#include "lib/fsl/vmo/strings.h"
#include "peridot/bin/user_runner/storage/constants_and_utils.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/fidl/json_xdr.h"

namespace modular {
namespace {

void XdrTriggerInfo_v1(XdrContext* const xdr,
                       AgentRunnerStorage::TriggerInfo* const data) {
  xdr->Field("agent_url", &data->agent_url);
  xdr->Field("task_id", &data->task_id);
  xdr->Field("task_type", &data->task_type);
  xdr->Field("alarm_in_seconds", &data->alarm_in_seconds);
  xdr->Field("queue_name", &data->queue_name);
  xdr->Field("queue_token", &data->queue_token);
}

void XdrTriggerInfo_v2(XdrContext* const xdr,
                       AgentRunnerStorage::TriggerInfo* const data) {
  if (!xdr->Version(2)) {
    return;
  }
  xdr->Field("agent_url", &data->agent_url);
  xdr->Field("task_id", &data->task_id);
  xdr->Field("task_type", &data->task_type);
  xdr->Field("alarm_in_seconds", &data->alarm_in_seconds);
  xdr->Field("queue_name", &data->queue_name);
  xdr->Field("queue_token", &data->queue_token);
}

constexpr XdrFilterType<AgentRunnerStorage::TriggerInfo> XdrTriggerInfo[] = {
    XdrTriggerInfo_v2,
    XdrTriggerInfo_v1,
    nullptr,
};

}  // namespace

class AgentRunnerStorageImpl::InitializeCall : public Operation<> {
 public:
  InitializeCall(NotificationDelegate* const delegate,
                 fuchsia::ledger::PageSnapshotPtr snapshot,
                 std::function<void()> done)
      : Operation("AgentRunnerStorageImpl::InitializeCall", std::move(done)),
        delegate_(delegate),
        snapshot_(std::move(snapshot)) {}

 private:
  void Run() override {
    FlowToken flow{this};

    GetEntries(snapshot_.get(), &entries_,
               [this, flow](fuchsia::ledger::Status status) {
                 if (status != fuchsia::ledger::Status::OK) {
                   FXL_LOG(ERROR) << trace_name() << " "
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
      std::string key(reinterpret_cast<const char*>(entry.key->data()),
                      entry.key->size());
      std::string value;
      if (!fsl::StringFromVmo(*entry.value, &value)) {
        FXL_LOG(ERROR) << trace_name() << " " << key << " "
                       << "VMO could nt be copied.";
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
  fuchsia::ledger::PageSnapshotPtr snapshot_;
  std::vector<fuchsia::ledger::Entry> entries_;
  FXL_DISALLOW_COPY_AND_ASSIGN(InitializeCall);
};

class AgentRunnerStorageImpl::WriteTaskCall : public Operation<bool> {
 public:
  WriteTaskCall(AgentRunnerStorageImpl* storage, std::string agent_url,
                TriggerInfo data, std::function<void(bool)> done)
      : Operation("AgentRunnerStorageImpl::WriteTaskCall", done),
        storage_(storage),
        agent_url_(std::move(agent_url)),
        data_(std::move(data)) {}

 private:
  void Run() override {
    FlowToken flow{this, &success_result_};

    std::string key = MakeTriggerKey(agent_url_, data_.task_id);
    std::string value;
    XdrWrite(&value, &data_, XdrTriggerInfo);

    storage_->page()->PutWithPriority(
        to_array(key), to_array(value), fuchsia::ledger::Priority::EAGER,
        [this, key, flow](fuchsia::ledger::Status status) {
          if (status != fuchsia::ledger::Status::OK) {
            FXL_LOG(ERROR) << trace_name() << " " << key << " "
                           << "Page.PutWithPriority() " << status;
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

class AgentRunnerStorageImpl::DeleteTaskCall : public Operation<bool> {
 public:
  DeleteTaskCall(AgentRunnerStorageImpl* storage, std::string agent_url,
                 std::string task_id, std::function<void(bool)> done)
      : Operation("AgentRunnerStorageImpl::DeleteTaskCall", done),
        storage_(storage),
        agent_url_(std::move(agent_url)),
        task_id_(std::move(task_id)) {}

 private:
  void Run() override {
    FlowToken flow{this, &success_result_};

    std::string key = MakeTriggerKey(agent_url_, task_id_);
    storage_->page()->Delete(
        to_array(key), [this, key, flow](fuchsia::ledger::Status status) {
          // fuchsia::ledger::Status::INVALID_TOKEN is okay because we might
          // have gotten a request to delete a token which does not exist. This
          // is okay.
          if (status != fuchsia::ledger::Status::OK &&
              status != fuchsia::ledger::Status::INVALID_TOKEN) {
            FXL_LOG(ERROR) << trace_name() << " " << key << " "
                           << "Page.Delete() " << status;
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
                                               fuchsia::ledger::PageId page_id)
    : PageClient("AgentRunnerStorageImpl", ledger_client, std::move(page_id)),
      delegate_(nullptr) {}

AgentRunnerStorageImpl::~AgentRunnerStorageImpl() = default;

void AgentRunnerStorageImpl::Initialize(NotificationDelegate* const delegate,
                                        std::function<void()> done) {
  FXL_DCHECK(!delegate_);
  delegate_ = delegate;
  operation_queue_.Add(
      new InitializeCall(delegate_, NewSnapshot(), std::move(done)));
}

void AgentRunnerStorageImpl::WriteTask(const std::string& agent_url,
                                       const TriggerInfo data,
                                       std::function<void(bool)> done) {
  operation_queue_.Add(
      new WriteTaskCall(this, agent_url, data, std::move(done)));
}

void AgentRunnerStorageImpl::DeleteTask(const std::string& agent_url,
                                        const std::string& task_id,
                                        std::function<void(bool)> done) {
  operation_queue_.Add(
      new DeleteTaskCall(this, agent_url, task_id, std::move(done)));
}

void AgentRunnerStorageImpl::OnPageChange(const std::string& key,
                                          const std::string& value) {
  FXL_DCHECK(delegate_ != nullptr);
  operation_queue_.Add(new SyncCall([this, key, value] {
    TriggerInfo data;
    if (!XdrRead(value, &data, XdrTriggerInfo)) {
      return;
    }
    delegate_->AddedTask(key, data);
  }));
}

void AgentRunnerStorageImpl::OnPageDelete(const std::string& key) {
  FXL_DCHECK(delegate_ != nullptr);
  operation_queue_.Add(
      new SyncCall([this, key] { delegate_->DeletedTask(key); }));
}

}  // namespace modular
