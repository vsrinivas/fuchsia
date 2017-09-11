// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_PUBLIC_LEDGER_STORAGE_H_
#define APPS_LEDGER_SRC_STORAGE_PUBLIC_LEDGER_STORAGE_H_

#include <memory>

#include "apps/ledger/src/storage/public/page_storage.h"
#include "apps/ledger/src/storage/public/types.h"
#include "lib/fxl/macros.h"

namespace storage {

// Manages storage for a single Ledger instance.
class LedgerStorage {
 public:
  LedgerStorage() {}
  virtual ~LedgerStorage() {}

  // Creates a new |PageStorage| for the Page with the given |page_id|.
  virtual void CreatePageStorage(
      PageId page_id,
      std::function<void(Status, std::unique_ptr<PageStorage>)> callback) = 0;

  // Finds the |PageStorage| corresponding to the page with the given |page_id|.
  // The result will be returned through the given |callback|. If the storage
  // for the given page doesn't exist nullptr will be returned instead.
  virtual void GetPageStorage(
      PageId page_id,
      std::function<void(Status, std::unique_ptr<PageStorage>)> callback) = 0;

  // Deletes the storage related to the page with |page_id|. This includes all
  // commits, tree nodes and objects.
  virtual bool DeletePageStorage(PageIdView page_id) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerStorage);
};

}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_PUBLIC_LEDGER_STORAGE_H_
