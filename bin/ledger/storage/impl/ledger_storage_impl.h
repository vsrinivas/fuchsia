// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_IMPL_LEDGER_STORAGE_IMPL_H_
#define APPS_LEDGER_SRC_STORAGE_IMPL_LEDGER_STORAGE_IMPL_H_

#include "apps/ledger/src/storage/public/ledger_storage.h"

#include <string>

#include "lib/ftl/tasks/task_runner.h"

namespace storage {

class LedgerStorageImpl : public LedgerStorage {
 public:
  LedgerStorageImpl(ftl::RefPtr<ftl::TaskRunner> main_runner,
                    ftl::RefPtr<ftl::TaskRunner> io_runner,
                    const std::string& base_storage_dir,
                    const std::string& ledger_name);
  ~LedgerStorageImpl() override;

  Status CreatePageStorage(PageIdView page_id,
                           std::unique_ptr<PageStorage>* page_storage) override;

  void GetPageStorage(
      PageIdView page_id,
      const std::function<void(Status, std::unique_ptr<PageStorage>)>& callback)
      override;

  bool DeletePageStorage(PageIdView page_id) override;

 private:
  std::string GetPathFor(PageIdView page_id);

  ftl::RefPtr<ftl::TaskRunner> main_runner_;
  ftl::RefPtr<ftl::TaskRunner> io_runner_;
  std::string storage_dir_;
};

}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_IMPL_LEDGER_STORAGE_IMPL_H_
