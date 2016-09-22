// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_STORAGE_IMPL_LEDGER_STORAGE_IMPL_H_
#define APPS_LEDGER_STORAGE_IMPL_LEDGER_STORAGE_IMPL_H_

#include "apps/ledger/storage/public/ledger_storage.h"

#include <memory>
#include <string>

#include "apps/ledger/storage/public/application_storage.h"

#include "lib/ftl/tasks/task_runner.h"

namespace storage {
class LedgerStorageImpl : public LedgerStorage {
 public:
  // Creates a new LedgerStorageImpl. |task_runner| executes long-running
  // storage tasks. |base_storage_dir| is the base directory on the local
  // filesystem used to store the ledger data.
  LedgerStorageImpl(ftl::TaskRunner* task_runner, std::string base_storage_dir);
  ~LedgerStorageImpl() override;

  // LedgerStorage:
  std::unique_ptr<ApplicationStorage> CreateApplicationStorage(
      std::string identity) override;

 private:
  ftl::TaskRunner* task_runner_;
  std::string base_storage_dir_;
};

}  // namespace storage

#endif  // APPS_LEDGER_STORAGE_IMPL_LEDGER_STORAGE_IMPL_H_
