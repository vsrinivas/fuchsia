// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/sync_coordinator/impl/user_sync_impl.h"

#include "src/ledger/bin/sync_coordinator/impl/ledger_sync_impl.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/logging/logging.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace sync_coordinator {

UserSyncImpl::UserSyncImpl(std::unique_ptr<cloud_sync::UserSync> cloud_sync,
                           std::unique_ptr<p2p_sync::UserCommunicator> p2p_sync)
    : cloud_sync_(std::move(cloud_sync)), p2p_sync_(std::move(p2p_sync)) {
  LEDGER_DCHECK(cloud_sync_ || p2p_sync_);
}

UserSyncImpl::~UserSyncImpl() = default;

void UserSyncImpl::Start() {
  LEDGER_DCHECK(!started_);
  started_ = true;
  if (cloud_sync_) {
    cloud_sync_->Start();
  }
  if (p2p_sync_) {
    p2p_sync_->Start();
  }
}
void UserSyncImpl::SetWatcher(SyncStateWatcher* watcher) {
  watcher_ = std::make_unique<SyncWatcherConverter>(watcher);
  if (cloud_sync_) {
    cloud_sync_->SetSyncWatcher(watcher_.get());
  }
}

std::unique_ptr<LedgerSync> UserSyncImpl::CreateLedgerSync(
    absl::string_view app_id, encryption::EncryptionService* encryption_service) {
  LEDGER_DCHECK(started_);
  std::unique_ptr<cloud_sync::LedgerSync> cloud_ledger_sync;
  if (cloud_sync_) {
    cloud_ledger_sync = cloud_sync_->CreateLedgerSync(app_id, encryption_service);
  }
  std::unique_ptr<p2p_sync::LedgerCommunicator> p2p_ledger_sync;
  if (p2p_sync_) {
    // FIXME(etiennej): fix the API
    p2p_ledger_sync = p2p_sync_->GetLedgerCommunicator(convert::ToString(app_id));
  }
  auto combined_sync =
      std::make_unique<LedgerSyncImpl>(std::move(cloud_ledger_sync), std::move(p2p_ledger_sync));
  return combined_sync;
}

}  // namespace sync_coordinator
