// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/agent_runner/agent_runner_storage_impl.h"

#include <fuchsia/ledger/cpp/fidl.h>
#include <lib/fsl/vmo/strings.h>

#include <functional>
#include <utility>

#include "src/modular/bin/sessionmgr/storage/constants_and_utils.h"
#include "src/modular/lib/async/cpp/operation.h"
#include "src/modular/lib/fidl/array_to_string.h"
#include "src/modular/lib/fidl/json_xdr.h"

namespace modular {
namespace {

void XdrTriggerInfo_v1(XdrContext* const xdr, AgentRunnerStorage::TriggerInfo* const data) {
  xdr->Field("agent_url", &data->agent_url);
  xdr->Field("task_id", &data->task_id);
  xdr->Field("task_type", &data->task_type);
  xdr->Field("alarm_in_seconds", &data->alarm_in_seconds);
  xdr->Field("queue_name", &data->queue_name);
  xdr->Field("queue_token", &data->queue_token);
}

void XdrTriggerInfo_v2(XdrContext* const xdr, AgentRunnerStorage::TriggerInfo* const data) {
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
  InitializeCall(NotificationDelegate* const delegate, fuchsia::ledger::PageSnapshotPtr snapshot,
                 fit::function<void()> done)
      : Operation("AgentRunnerStorageImpl::InitializeCall", std::move(done)),
        delegate_(delegate),
        snapshot_(std::move(snapshot)) {}

 private:
  void Run() override {
    FlowToken flow{this};

    GetEntries(snapshot_.get(), &entries_, [this, flow] { Cont(flow); });
  }

  void Cont(FlowToken /*flow*/) {
    if (entries_.empty()) {
      // No existing entries.
      return;
    }

    for (const auto& entry : entries_) {
      std::string key(reinterpret_cast<const char*>(entry.key.data()), entry.key.size());
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
};

AgentRunnerStorageImpl::AgentRunnerStorageImpl(LedgerClient* ledger_client,
                                               fuchsia::ledger::PageId page_id)
    : PageClient("AgentRunnerStorageImpl", ledger_client, std::move(page_id)), delegate_(nullptr) {}

AgentRunnerStorageImpl::~AgentRunnerStorageImpl() = default;

void AgentRunnerStorageImpl::Initialize(NotificationDelegate* const delegate,
                                        fit::function<void()> done) {
  FXL_DCHECK(!delegate_);
  delegate_ = delegate;
  operation_queue_.Add(std::make_unique<InitializeCall>(delegate_, NewSnapshot(), std::move(done)));
}

void AgentRunnerStorageImpl::OnPageChange(const std::string& key, const std::string& value) {
  FXL_DCHECK(delegate_ != nullptr);
  operation_queue_.Add(std::make_unique<SyncCall>([this, key, value] {
    TriggerInfo data;
    if (!XdrRead(value, &data, XdrTriggerInfo)) {
      return;
    }
    delegate_->AddedTask(key, data);
  }));
}

void AgentRunnerStorageImpl::OnPageDelete(const std::string& key) {
  FXL_DCHECK(delegate_ != nullptr);
  operation_queue_.Add(std::make_unique<SyncCall>([this, key] { delegate_->DeletedTask(key); }));
}

}  // namespace modular
