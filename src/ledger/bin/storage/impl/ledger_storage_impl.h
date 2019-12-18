// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_LEDGER_STORAGE_IMPL_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_LEDGER_STORAGE_IMPL_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>

#include <set>

#include "src/ledger/bin/clocks/public/device_id_manager.h"
#include "src/ledger/bin/encryption/public/encryption_service.h"
#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/storage/public/db.h"
#include "src/ledger/bin/storage/public/db_factory.h"
#include "src/ledger/bin/storage/public/ledger_storage.h"
#include "src/ledger/lib/coroutine/coroutine.h"
#include "src/ledger/lib/files/detached_path.h"
#include "src/ledger/lib/memory/weak_ptr.h"

namespace storage {

class LedgerStorageImpl : public LedgerStorage {
 public:
  LedgerStorageImpl(ledger::Environment* environment,
                    encryption::EncryptionService* encryption_service,
                    storage::DbFactory* db_factory, ledger::DetachedPath content_dir,
                    CommitPruningPolicy policy, clocks::DeviceIdManager* device_id_manager);
  LedgerStorageImpl(const LedgerStorageImpl&) = delete;
  LedgerStorageImpl& operator=(const LedgerStorageImpl&) = delete;
  ~LedgerStorageImpl() override;

  // Initializes this LedgerStorageImpl by creating the |content_dir| directory
  // given in the constructor.
  Status Init();

  // LedgerStorage:
  void ListPages(fit::function<void(Status, std::set<PageId>)> callback) override;
  void CreatePageStorage(
      PageId page_id, fit::function<void(Status, std::unique_ptr<PageStorage>)> callback) override;

  void GetPageStorage(PageId page_id,
                      fit::function<void(Status, std::unique_ptr<PageStorage>)> callback) override;

  void DeletePageStorage(PageIdView page_id, fit::function<void(Status)> callback) override;

 private:
  // Creates and returns through the callback, an initialized |PageStorageImpl|
  // object.
  void InitializePageStorage(PageId page_id, std::unique_ptr<Db> db,
                             fit::function<void(Status, std::unique_ptr<PageStorage>)> callback);

  // Gets or creates a new PageStorage at the given |path| for the page with the
  // given |page_id|.
  void GetOrCreateDb(ledger::DetachedPath path, PageId page_id,
                     DbFactory::OnDbNotFound on_db_not_found,
                     fit::function<void(Status, std::unique_ptr<PageStorage>)> callback);

  ledger::DetachedPath GetPathFor(PageIdView page_id);

  ledger::Environment* const environment_;
  encryption::EncryptionService* const encryption_service_;
  storage::DbFactory* const db_factory_;
  ledger::DetachedPath storage_dir_;
  ledger::DetachedPath staging_dir_;
  // Keep track of all PageStorage instances currently in initialization. This
  // ensures that any created PageStorage that has not yet been passed to the
  // caller will be deleted when this object is deleted.
  std::map<PageStorage*, std::unique_ptr<PageStorage>> storage_in_initialization_;

  // Pruning policy for all pages created in this ledger.
  CommitPruningPolicy pruning_policy_;
  // Manager used to generate device IDs for new pages.
  clocks::DeviceIdManager* const device_id_manager_;

  // This must be the last member of the class.
  ledger::WeakPtrFactory<LedgerStorageImpl> weak_factory_;
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_LEDGER_STORAGE_IMPL_H_
