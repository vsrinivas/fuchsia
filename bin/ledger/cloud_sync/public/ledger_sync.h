// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_CLOUD_SYNC_PUBLIC_LEDGER_SYNC_H_
#define PERIDOT_BIN_LEDGER_CLOUD_SYNC_PUBLIC_LEDGER_SYNC_H_

#include <functional>
#include <memory>
#include <string>

#include <lib/fit/function.h>
#include <lib/fxl/macros.h>

#include "peridot/bin/ledger/cloud_sync/public/page_sync.h"
#include "peridot/bin/ledger/encryption/public/encryption_service.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"

namespace cloud_sync {

// Manages Cloud Sync for a particular ledger.
class LedgerSync {
 public:
  LedgerSync() {}
  virtual ~LedgerSync() {}

  // Creates a new page sync for the given page. The page could already have
  // data synced to the cloud or not.
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

}  // namespace cloud_sync

#endif  // PERIDOT_BIN_LEDGER_CLOUD_SYNC_PUBLIC_LEDGER_SYNC_H_
