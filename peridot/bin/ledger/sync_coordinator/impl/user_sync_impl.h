// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_SYNC_COORDINATOR_IMPL_USER_SYNC_IMPL_H_
#define PERIDOT_BIN_LEDGER_SYNC_COORDINATOR_IMPL_USER_SYNC_IMPL_H_

#include <memory>

#include <lib/fxl/macros.h>
#include <lib/fxl/strings/string_view.h>

#include "peridot/bin/ledger/cloud_sync/public/user_sync.h"
#include "peridot/bin/ledger/encryption/public/encryption_service.h"
#include "peridot/bin/ledger/p2p_sync/public/user_communicator.h"
#include "peridot/bin/ledger/sync_coordinator/impl/sync_watcher_converter.h"
#include "peridot/bin/ledger/sync_coordinator/public/ledger_sync.h"
#include "peridot/bin/ledger/sync_coordinator/public/user_sync.h"

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
      fxl::StringView app_id,
      encryption::EncryptionService* encryption_service) override;

 private:
  std::unique_ptr<SyncWatcherConverter> watcher_;
  bool started_ = false;

  std::unique_ptr<cloud_sync::UserSync> const cloud_sync_;
  std::unique_ptr<p2p_sync::UserCommunicator> const p2p_sync_;
};

}  // namespace sync_coordinator

#endif  // PERIDOT_BIN_LEDGER_SYNC_COORDINATOR_IMPL_USER_SYNC_IMPL_H_
