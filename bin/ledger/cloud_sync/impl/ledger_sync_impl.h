// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_LEDGER_SYNC_IMPL_H_
#define APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_LEDGER_SYNC_IMPL_H_

#include "apps/ledger/src/cloud_sync/public/ledger_sync.h"
#include "apps/ledger/src/configuration/configuration.h"
#include "apps/ledger/src/environment/environment.h"
#include "apps/ledger/src/network/network_service.h"

namespace cloud_sync {

class LedgerSyncImpl : public LedgerSync {
 public:
  LedgerSyncImpl(ledger::Environment* environment, ftl::StringView app_id);
  ~LedgerSyncImpl();

  std::unique_ptr<PageSyncContext> CreatePageContext(
      storage::PageStorage* page_storage,
      ftl::Closure error_callback) override;

 private:
  ledger::Environment* environment_;
  const std::string app_id_;
};

}  // namespace cloud_sync

#endif  // APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_LEDGER_SYNC_IMPL_H_
