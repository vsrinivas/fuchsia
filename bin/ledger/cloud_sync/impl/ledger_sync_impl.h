// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_LEDGER_SYNC_IMPL_H_
#define APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_LEDGER_SYNC_IMPL_H_

#include "apps/ledger/src/cloud_sync/public/ledger_sync.h"
#include "apps/ledger/src/cloud_sync/public/user_config.h"
#include "apps/ledger/src/environment/environment.h"
#include "apps/ledger/src/firebase/firebase.h"
#include "apps/ledger/src/network/network_service.h"

namespace cloud_sync {

class LedgerSyncImpl : public LedgerSync {
 public:
  LedgerSyncImpl(ledger::Environment* environment,
                 const UserConfig* user_config,
                 ftl::StringView app_id);
  ~LedgerSyncImpl();

  void RemoteContains(ftl::StringView page_id,
                      std::function<void(RemoteResponse)> callback) override;

  std::unique_ptr<PageSyncContext> CreatePageContext(
      storage::PageStorage* page_storage,
      ftl::Closure error_callback) override;

 private:
  ledger::Environment* const environment_;
  const UserConfig* const user_config_;
  const std::string app_gcs_prefix_;
  // Firebase path under which the data of this Ledger instance is stored.
  const std::string app_firebase_path_;
  // Firebase instance scoped to |app_path_|.
  std::unique_ptr<firebase::Firebase> app_firebase_;
};

}  // namespace cloud_sync

#endif  // APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_LEDGER_SYNC_IMPL_H_
