// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_IMPL_LEDGER_STORAGE_IMPL_H_
#define PERIDOT_BIN_LEDGER_STORAGE_IMPL_LEDGER_STORAGE_IMPL_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/fxl/memory/weak_ptr.h>

#include "peridot/bin/ledger/coroutine/coroutine.h"
#include "peridot/bin/ledger/encryption/public/encryption_service.h"
#include "peridot/bin/ledger/environment/environment.h"
#include "peridot/bin/ledger/filesystem/detached_path.h"
#include "peridot/bin/ledger/storage/public/db.h"
#include "peridot/bin/ledger/storage/public/db_factory.h"
#include "peridot/bin/ledger/storage/public/ledger_storage.h"

namespace storage {

class LedgerStorageImpl : public LedgerStorage {
 public:
  LedgerStorageImpl(ledger::Environment* environment,
                    encryption::EncryptionService* encryption_service,
                    storage::DbFactory* db_factory,
                    ledger::DetachedPath content_dir);
  ~LedgerStorageImpl() override;

  // Initializes this LedgerStorageImpl by creating the |content_dir| directory
  // given in the constructor.
  Status Init();

  // LedgerStorage:
  void CreatePageStorage(
      PageId page_id,
      fit::function<void(Status, std::unique_ptr<PageStorage>)> callback)
      override;

  void GetPageStorage(PageId page_id,
                      fit::function<void(Status, std::unique_ptr<PageStorage>)>
                          callback) override;

  void DeletePageStorage(PageIdView page_id,
                         fit::function<void(Status)> callback) override;

  // For debugging only.
  std::vector<PageId> ListLocalPages();

 private:
  // Creates and returns through the callback, an initialized |PageStorageImpl|
  // object.
  void InitializePageStorage(
      PageId page_id, std::unique_ptr<Db> db,
      fit::function<void(Status, std::unique_ptr<PageStorage>)> callback);

  // Gets or creates a new PageStorage at the given |path| for the page with the
  // given |page_id|.
  void GetOrCreateDb(
      ledger::DetachedPath path, PageId page_id,
      fit::function<void(Status, std::unique_ptr<PageStorage>)> callback);

  ledger::DetachedPath GetPathFor(PageIdView page_id);
  ledger::DetachedPath GetDeprecatedPathFor(PageIdView page_id);

  // Returns the staging path for the given |page_id|.
  ledger::DetachedPath GetStagingPathFor(PageIdView page_id);

  ledger::Environment* const environment_;
  encryption::EncryptionService* const encryption_service_;
  storage::DbFactory* const db_factory_;
  ledger::DetachedPath storage_dir_;

  // This must be the last member of the class.
  fxl::WeakPtrFactory<LedgerStorageImpl> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerStorageImpl);
};

}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_IMPL_LEDGER_STORAGE_IMPL_H_
