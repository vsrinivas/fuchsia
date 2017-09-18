// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_SYNC_PUBLIC_LEDGER_SYNC_H_
#define APPS_LEDGER_SRC_CLOUD_SYNC_PUBLIC_LEDGER_SYNC_H_

#include <functional>
#include <memory>
#include <string>

#include "apps/ledger/src/cloud_provider/public/page_cloud_handler.h"
#include "apps/ledger/src/cloud_sync/public/page_sync.h"
#include "apps/ledger/src/firebase/firebase.h"
#include "apps/ledger/src/gcs/cloud_storage.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "lib/fxl/macros.h"

namespace cloud_sync {

// Page sync along with associated objects that it uses.
struct PageSyncContext {
  std::unique_ptr<firebase::Firebase> firebase;
  std::unique_ptr<gcs::CloudStorage> cloud_storage;
  std::unique_ptr<cloud_provider_firebase::PageCloudHandler> cloud_provider;
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

#endif  // APPS_LEDGER_SRC_CLOUD_SYNC_PUBLIC_LEDGER_SYNC_H_
