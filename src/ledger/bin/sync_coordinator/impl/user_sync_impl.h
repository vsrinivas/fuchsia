// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_SYNC_COORDINATOR_IMPL_USER_SYNC_IMPL_H_
#define SRC_LEDGER_BIN_SYNC_COORDINATOR_IMPL_USER_SYNC_IMPL_H_

#include <memory>

#include "src/ledger/bin/cloud_sync/public/user_sync.h"
#include "src/ledger/bin/encryption/public/encryption_service.h"
#include "src/ledger/bin/p2p_sync/public/user_communicator.h"
#include "src/ledger/bin/sync_coordinator/impl/sync_watcher_converter.h"
#include "src/ledger/bin/sync_coordinator/public/ledger_sync.h"
#include "src/ledger/bin/sync_coordinator/public/user_sync.h"
#include "src/lib/fxl/strings/string_view.h"

namespace sync_coordinator {

class UserSyncImpl : public UserSync {
 public:
  UserSyncImpl(std::unique_ptr<cloud_sync::UserSync> cloud_sync,
               std::unique_ptr<p2p_sync::UserCommunicator> p2p_sync);
  ~UserSyncImpl() override;

  // UserSync:
  void Start() override;
  void SetWatcher(SyncStateWatcher* watcher) override;
  std::unique_ptr<LedgerSync> CreateLedgerSync(
      fxl::StringView app_id, encryption::EncryptionService* encryption_service) override;

 private:
  std::unique_ptr<SyncWatcherConverter> watcher_;
  bool started_ = false;

  std::unique_ptr<cloud_sync::UserSync> const cloud_sync_;
  std::unique_ptr<p2p_sync::UserCommunicator> const p2p_sync_;
};

}  // namespace sync_coordinator

#endif  // SRC_LEDGER_BIN_SYNC_COORDINATOR_IMPL_USER_SYNC_IMPL_H_
