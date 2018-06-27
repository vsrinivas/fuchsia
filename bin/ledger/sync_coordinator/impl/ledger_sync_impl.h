// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_SYNC_COORDINATOR_IMPL_LEDGER_SYNC_IMPL_H_
#define PERIDOT_BIN_LEDGER_SYNC_COORDINATOR_IMPL_LEDGER_SYNC_IMPL_H_

#include <functional>
#include <memory>

#include <lib/fit/function.h>

#include "peridot/bin/ledger/cloud_sync/public/ledger_sync.h"
#include "peridot/bin/ledger/p2p_sync/public/ledger_communicator.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"
#include "peridot/bin/ledger/sync_coordinator/public/ledger_sync.h"
#include "peridot/bin/ledger/sync_coordinator/public/page_sync.h"

namespace sync_coordinator {

class LedgerSyncImpl : public LedgerSync {
 public:
  LedgerSyncImpl(std::unique_ptr<cloud_sync::LedgerSync> cloud_sync,
                 std::unique_ptr<p2p_sync::LedgerCommunicator> p2p_sync);
  ~LedgerSyncImpl() override;

  // LedgerSync:
  std::unique_ptr<PageSync> CreatePageSync(
      storage::PageStorage* page_storage,
      storage::PageSyncClient* page_sync_client,
      fit::closure error_callback) override;

 private:
  std::unique_ptr<cloud_sync::LedgerSync> const cloud_sync_;
  std::unique_ptr<p2p_sync::LedgerCommunicator> const p2p_sync_;
};

}  // namespace sync_coordinator

#endif  // PERIDOT_BIN_LEDGER_SYNC_COORDINATOR_IMPL_LEDGER_SYNC_IMPL_H_
