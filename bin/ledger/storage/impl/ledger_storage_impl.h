// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_IMPL_LEDGER_STORAGE_IMPL_H_
#define PERIDOT_BIN_LEDGER_STORAGE_IMPL_LEDGER_STORAGE_IMPL_H_

#include <string>

#include "lib/fxl/tasks/task_runner.h"
#include "peridot/bin/ledger/coroutine/coroutine.h"
#include "peridot/bin/ledger/storage/public/ledger_storage.h"

namespace storage {

class LedgerStorageImpl : public LedgerStorage {
 public:
  LedgerStorageImpl(coroutine::CoroutineService* coroutine_service,
                    const std::string& base_storage_dir,
                    const std::string& ledger_name);
  ~LedgerStorageImpl() override;

  void CreatePageStorage(
      PageId page_id,
      std::function<void(Status, std::unique_ptr<PageStorage>)> callback)
      override;

  void GetPageStorage(PageId page_id,
                      std::function<void(Status, std::unique_ptr<PageStorage>)>
                          callback) override;

  bool DeletePageStorage(PageIdView page_id) override;

  // For debugging only.
  std::vector<PageId> ListLocalPages();

 private:
  std::string GetPathFor(PageIdView page_id);

  fxl::RefPtr<fxl::TaskRunner> main_runner_;
  coroutine::CoroutineService* const coroutine_service_;
  std::string storage_dir_;
};

}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_IMPL_LEDGER_STORAGE_IMPL_H_
