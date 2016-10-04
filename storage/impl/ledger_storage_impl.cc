// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include "apps/ledger/storage/impl/ledger_storage_impl.h"

#include "apps/ledger/storage/impl/application_storage_impl.h"
#include "lib/ftl/tasks/task_runner.h"

namespace storage {

LedgerStorageImpl::LedgerStorageImpl(ftl::RefPtr<ftl::TaskRunner> task_runner,
                                     std::string base_storage_dir)
    : task_runner_(std::move(task_runner)),
      base_storage_dir_(base_storage_dir) {}

LedgerStorageImpl::~LedgerStorageImpl() {}

std::unique_ptr<ApplicationStorage> LedgerStorageImpl::CreateApplicationStorage(
    std::string identity) {
  return std::unique_ptr<ApplicationStorage>(new ApplicationStorageImpl(
      task_runner_, base_storage_dir_ + "/" + identity));
}

}  // namespace storage
