// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_SYNC_COORDINATOR_PUBLIC_LEDGER_SYNC_H_
#define PERIDOT_BIN_LEDGER_SYNC_COORDINATOR_PUBLIC_LEDGER_SYNC_H_

#include <functional>
#include <memory>

#include <lib/fit/function.h>

#include "lib/fxl/macros.h"
#include "peridot/bin/ledger/cloud_sync/public/ledger_sync.h"
#include "peridot/bin/ledger/p2p_sync/public/ledger_communicator.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"
#include "peridot/bin/ledger/sync_coordinator/public/page_sync.h"

namespace sync_coordinator {

// Manages synchronization for a ledger.
class LedgerSync {
 public:
  LedgerSync() {}
  virtual ~LedgerSync() {}

  // Creates a new page sync for the given page.
  //
  // The provided |error_callback| is called when sync is stopped due to an
  // unrecoverable error.
  virtual std::unique_ptr<PageSync> CreatePageSync(
      storage::PageStorage* page_storage,
      storage::PageSyncClient* page_sync_client,
      fit::closure error_callback) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerSync);
};

}  // namespace sync_coordinator

#endif  // PERIDOT_BIN_LEDGER_SYNC_COORDINATOR_PUBLIC_LEDGER_SYNC_H_
