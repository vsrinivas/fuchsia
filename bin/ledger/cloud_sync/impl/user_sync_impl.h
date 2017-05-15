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

namespace cloud_sync {

class UserSyncImpl : public UserSync {
 public:
  UserSyncImpl(ledger::Environment* environment, UserConfig user_config);
  ~UserSyncImpl() override;

 private:
  // UserSync
  const UserConfig& GetUserConfig() override;
  std::unique_ptr<LedgerSync> CreateLedgerSync(ftl::StringView app_id) override;

  ledger::Environment* environment_;
  UserConfig user_config_;
  std::unordered_set<LedgerSyncImpl*> active_ledger_syncs_;
};

}  // namespace cloud_sync

#endif  // APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_USER_SYNC_IMPL_H_
