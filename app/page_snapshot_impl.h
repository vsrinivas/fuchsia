// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_APP_PAGE_SNAPSHOT_IMPL_H_
#define APPS_LEDGER_APP_PAGE_SNAPSHOT_IMPL_H_

#include <memory>

#include "apps/ledger/api/ledger.mojom.h"
#include "apps/ledger/storage/public/commit_contents.h"
#include "apps/ledger/storage/public/page_storage.h"
#include "lib/ftl/tasks/task_runner.h"

namespace ledger {

class PageSnapshotImpl : public PageSnapshot {
 public:
  PageSnapshotImpl(storage::PageStorage* page_storage,
                   std::unique_ptr<storage::CommitContents> contents);
  ~PageSnapshotImpl();

 private:
  // PageSnapshot:
  void GetEntries(mojo::Array<uint8_t> key_prefix,
                  const GetEntriesCallback& callback) override;
  void GetKeys(mojo::Array<uint8_t> key_prefix,
               const GetKeysCallback& callback) override;
  void Get(mojo::Array<uint8_t> key, const GetCallback& callback) override;
  void GetPartial(mojo::Array<uint8_t> key,
                  int64_t offset,
                  int64_t max_size,
                  const GetPartialCallback& callback) override;

  storage::PageStorage* page_storage_;
  std::unique_ptr<storage::CommitContents> contents_;
};

}  // namespace ledger

#endif  // APPS_LEDGER_APP_PAGE_STORAGE_IMPL_H_
