// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_SYNC_PUBLIC_USER_SYNC_H_
#define APPS_LEDGER_SRC_CLOUD_SYNC_PUBLIC_USER_SYNC_H_

#include <memory>

#include "apps/ledger/src/cloud_sync/public/ledger_sync.h"
#include "apps/ledger/src/cloud_sync/public/sync_state_watcher.h"
#include "apps/ledger/src/cloud_sync/public/user_config.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/strings/string_view.h"

namespace cloud_sync {

// Top level factory for every object sync related for a given user.
class UserSync {
 public:
  UserSync() {}
  virtual ~UserSync() {}

  virtual std::unique_ptr<LedgerSync> CreateLedgerSync(
      ftl::StringView app_id) = 0;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(UserSync);
};

}  // namespace cloud_sync

#endif  // APPS_LEDGER_SRC_CLOUD_SYNC_PUBLIC_USER_SYNC_H_
