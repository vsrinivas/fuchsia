// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_CLOUD_SYNC_PUBLIC_USER_SYNC_H_
#define SRC_LEDGER_BIN_CLOUD_SYNC_PUBLIC_USER_SYNC_H_

#include <memory>

#include "src/ledger/bin/cloud_sync/public/ledger_sync.h"
#include "src/ledger/bin/cloud_sync/public/sync_state_watcher.h"
#include "src/ledger/bin/cloud_sync/public/user_config.h"
#include "src/ledger/bin/encryption/public/encryption_service.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace cloud_sync {

// Top level factory for every object sync related for a given user.
class UserSync {
 public:
  UserSync() = default;
  UserSync(const UserSync&) = delete;
  UserSync& operator=(const UserSync&) = delete;
  virtual ~UserSync() = default;

  // Sets a synchronization state watcher for this user.
  //
  // Set the watcher to nullptr to unregister a previously set watcher.
  virtual void SetSyncWatcher(SyncStateWatcher* watcher) = 0;

  // Starts the user synchronization.
  virtual void Start() = 0;

  virtual std::unique_ptr<LedgerSync> CreateLedgerSync(
      absl::string_view app_id, encryption::EncryptionService* encryption_service) = 0;
};

}  // namespace cloud_sync

#endif  // SRC_LEDGER_BIN_CLOUD_SYNC_PUBLIC_USER_SYNC_H_
