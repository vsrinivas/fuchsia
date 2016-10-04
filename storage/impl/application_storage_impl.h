// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_STORAGE_IMPL_APPLICATION_STORAGE_IMPL_H_
#define APPS_LEDGER_STORAGE_IMPL_APPLICATION_STORAGE_IMPL_H_

#include <string>

#include "apps/ledger/storage/public/application_storage.h"

#include "lib/ftl/tasks/task_runner.h"

namespace storage {

class ApplicationStorageImpl : public ApplicationStorage {
 public:
  ApplicationStorageImpl(ftl::RefPtr<ftl::TaskRunner> task_runner,
                         std::string storage_dir);
  ~ApplicationStorageImpl() override;

  std::unique_ptr<PageStorage> CreatePageStorage(
      const PageId& page_id) override;

  void GetPageStorage(const PageId& page_id,
                      const std::function<void(std::unique_ptr<PageStorage>)>&
                          callback) override;

  bool DeletePageStorage(const PageId& page_id) override;

 private:
  std::string GetPathFor(const PageId& page_id);

  ftl::RefPtr<ftl::TaskRunner> task_runner_;
  std::string storage_dir_;
};

}  // namespace storage

#endif  // APPS_LEDGER_STORAGE_IMPL_APPLICATION_STORAGE_IMPL_H_
