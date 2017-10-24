// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_CLOUD_SYNC_PUBLIC_LEDGER_SYNC_H_
#define PERIDOT_BIN_LEDGER_CLOUD_SYNC_PUBLIC_LEDGER_SYNC_H_

#include <functional>
#include <memory>
#include <string>

#include "lib/fxl/macros.h"
#include "peridot/bin/ledger/cloud_sync/public/page_sync.h"
#include "peridot/bin/ledger/encryption/public/encryption_service.h"
#include "peridot/bin/ledger/firebase/firebase.h"
#include "peridot/bin/ledger/gcs/cloud_storage.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"

namespace cloud_sync {

// Page sync along with associated objects that it uses.
struct PageSyncContext {
  // TODO(qsr): LE-330 Review the location of EncryptionService depending on
  // where it is used.
  std::unique_ptr<encryption::EncryptionService> encryption_service;
  std::unique_ptr<PageSync> page_sync;
};

// Manages Cloud Sync for a particular ledger.
class LedgerSync {
 public:
  LedgerSync() {}
  virtual ~LedgerSync() {}

  // Creates a new page sync along with its context for the given page. The page
  // could already have data synced to the cloud or not.
  //
  // The provided |error_callback| is called when sync is stopped due to an
  // unrecoverable error.
  virtual std::unique_ptr<PageSyncContext> CreatePageContext(
      storage::PageStorage* page_storage,
      fxl::Closure error_callback) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerSync);
};

}  // namespace cloud_sync

#endif  // PERIDOT_BIN_LEDGER_CLOUD_SYNC_PUBLIC_LEDGER_SYNC_H_
