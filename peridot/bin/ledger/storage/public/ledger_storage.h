// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_PUBLIC_LEDGER_STORAGE_H_
#define PERIDOT_BIN_LEDGER_STORAGE_PUBLIC_LEDGER_STORAGE_H_

#include <memory>

#include <lib/fit/function.h>
#include <lib/fxl/macros.h>

#include "peridot/bin/ledger/storage/public/page_storage.h"
#include "peridot/bin/ledger/storage/public/types.h"

namespace storage {

// Manages storage for a single Ledger instance.
class LedgerStorage {
 public:
  LedgerStorage() {}
  virtual ~LedgerStorage() {}

  // Creates a new |PageStorage| for the Page with the given |page_id|.
  virtual void CreatePageStorage(
      PageId page_id,
      fit::function<void(Status, std::unique_ptr<PageStorage>)> callback) = 0;

  // Finds the |PageStorage| corresponding to the page with the given |page_id|.
  // The result will be returned through the given |callback|. If the storage
  // for the given page isn't found locally, nullptr will be returned instead.
  virtual void GetPageStorage(
      PageId page_id,
      fit::function<void(Status, std::unique_ptr<PageStorage>)> callback) = 0;

  // Deletes the storage related to the page with |page_id|. This includes the
  // local copy of the page storage with all commits, tree nodes and values.
  // This method can fail with a |NOT_FOUND| error if the page is not present in
  // the local storage, or with an |IO_ERROR| if deletion fails.
  virtual void DeletePageStorage(PageIdView page_id,
                                 fit::function<void(Status)> callback) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerStorage);
};

}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_PUBLIC_LEDGER_STORAGE_H_
