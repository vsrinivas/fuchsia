// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_USER_SYNC_IMPL_H_
#define APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_USER_SYNC_IMPL_H_

#include "apps/ledger/src/cloud_sync/public/user_sync.h"

#include <memory>
#include <unordered_set>

#include "apps/ledger/src/cloud_sync/impl/ledger_sync_impl.h"
#include "apps/ledger/src/environment/environment.h"
#include "apps/ledger/src/firebase/firebase.h"

namespace cloud_sync {

class UserSyncImpl : public UserSync {
 public:
  UserSyncImpl(ledger::Environment* environment, UserConfig user_config);
  ~UserSyncImpl() override;

  // Starts UserSyncImpl. This method must be called before any other method.
  void Start();

 private:
  // Check that the version on the cloud is compatible with the local version on
  // the device.
  void CheckCloudVersion();

  // Enable sync upload.
  void EnableUpload();

  // UserSync
  std::unique_ptr<LedgerSync> CreateLedgerSync(ftl::StringView app_id) override;

  ledger::Environment* environment_;
  const UserConfig user_config_;

  // UserSyncImpl must be started before it can be used.
  bool started_ = false;
  // Whether uploads should be enabled. It is false until the cloud version has
  // been checked.
  bool upload_enabled_ = false;
  std::unique_ptr<firebase::Firebase> user_firebase_;
  std::unordered_set<LedgerSyncImpl*> active_ledger_syncs_;
};

}  // namespace cloud_sync

#endif  // APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_USER_SYNC_IMPL_H_
