// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_CLOUD_SYNC_PUBLIC_LEDGER_SYNC_H_
#define SRC_LEDGER_BIN_CLOUD_SYNC_PUBLIC_LEDGER_SYNC_H_

#include <lib/fit/function.h>

#include <functional>
#include <memory>
#include <string>

#include "src/ledger/bin/cloud_sync/public/page_sync.h"
#include "src/ledger/bin/encryption/public/encryption_service.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/lib/fxl/macros.h"

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
      storage::PageSyncClient* page_sync_client) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerSync);
};

}  // namespace cloud_sync

#endif  // SRC_LEDGER_BIN_CLOUD_SYNC_PUBLIC_LEDGER_SYNC_H_
