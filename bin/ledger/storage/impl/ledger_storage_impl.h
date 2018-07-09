// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_IMPL_LEDGER_STORAGE_IMPL_H_
#define PERIDOT_BIN_LEDGER_STORAGE_IMPL_LEDGER_STORAGE_IMPL_H_

#include <string>

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>

#include "peridot/bin/ledger/coroutine/coroutine.h"
#include "peridot/bin/ledger/encryption/public/encryption_service.h"
#include "peridot/bin/ledger/filesystem/detached_path.h"
#include "peridot/bin/ledger/storage/public/ledger_storage.h"

namespace storage {

class LedgerStorageImpl : public LedgerStorage {
 public:
  LedgerStorageImpl(async_t* async,
                    coroutine::CoroutineService* coroutine_service,
                    encryption::EncryptionService* encryption_service,
                    ledger::DetachedPath content_dir,
                    const std::string& ledger_name);
  ~LedgerStorageImpl() override;

  void CreatePageStorage(
      PageId page_id,
      fit::function<void(Status, std::unique_ptr<PageStorage>)> callback)
      override;

  void GetPageStorage(PageId page_id,
                      fit::function<void(Status, std::unique_ptr<PageStorage>)>
                          callback) override;

  Status DeletePageStorage(PageIdView page_id) override;

  // For debugging only.
  std::vector<PageId> ListLocalPages();

 private:
  ledger::DetachedPath GetPathFor(PageIdView page_id);

  // Returns the staging path for the given |page_id|.
  ledger::DetachedPath GetStagingPathFor(PageIdView page_id);

  async_t* const async_;
  coroutine::CoroutineService* const coroutine_service_;
  encryption::EncryptionService* const encryption_service_;
  ledger::DetachedPath storage_dir_;
};

}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_IMPL_LEDGER_STORAGE_IMPL_H_
