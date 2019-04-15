// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_SYNC_COORDINATOR_IMPL_LEDGER_SYNC_IMPL_H_
#define SRC_LEDGER_BIN_SYNC_COORDINATOR_IMPL_LEDGER_SYNC_IMPL_H_

#include <lib/fit/function.h>

#include <functional>
#include <memory>

#include "src/ledger/bin/cloud_sync/public/ledger_sync.h"
#include "src/ledger/bin/p2p_sync/public/ledger_communicator.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/bin/sync_coordinator/public/ledger_sync.h"
#include "src/ledger/bin/sync_coordinator/public/page_sync.h"

namespace sync_coordinator {

class LedgerSyncImpl : public LedgerSync {
 public:
  LedgerSyncImpl(std::unique_ptr<cloud_sync::LedgerSync> cloud_sync,
                 std::unique_ptr<p2p_sync::LedgerCommunicator> p2p_sync);
  ~LedgerSyncImpl() override;

  // LedgerSync:
  std::unique_ptr<PageSync> CreatePageSync(
      storage::PageStorage* page_storage,
      storage::PageSyncClient* page_sync_client) override;

 private:
  std::unique_ptr<cloud_sync::LedgerSync> const cloud_sync_;
  std::unique_ptr<p2p_sync::LedgerCommunicator> const p2p_sync_;
};

}  // namespace sync_coordinator

#endif  // SRC_LEDGER_BIN_SYNC_COORDINATOR_IMPL_LEDGER_SYNC_IMPL_H_
