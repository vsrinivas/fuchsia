// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_PAGE_SNAPSHOT_IMPL_H_
#define APPS_LEDGER_SRC_APP_PAGE_SNAPSHOT_IMPL_H_

#include <memory>

#include "lib/ledger/fidl/ledger.fidl.h"
#include "peridot/bin/ledger/storage/public/commit.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"
#include "lib/fxl/tasks/task_runner.h"

namespace ledger {

// An implementation of the |PageSnapshot| FIDL interface.
class PageSnapshotImpl : public PageSnapshot {
 public:
  PageSnapshotImpl(storage::PageStorage* page_storage,
                   std::unique_ptr<const storage::Commit> commit,
                   std::string key_prefix);
  ~PageSnapshotImpl() override;

 private:
  // PageSnapshot:
  void GetEntries(fidl::Array<uint8_t> key_start,
                  fidl::Array<uint8_t> token,
                  const GetEntriesCallback& callback) override;
  void GetEntriesInline(fidl::Array<uint8_t> key_start,
                        fidl::Array<uint8_t> token,
                        const GetEntriesInlineCallback& callback) override;
  void GetKeys(fidl::Array<uint8_t> key_start,
               fidl::Array<uint8_t> token,
               const GetKeysCallback& callback) override;
  void Get(fidl::Array<uint8_t> key, const GetCallback& callback) override;
  void GetInline(fidl::Array<uint8_t> key,
                 const GetInlineCallback& callback) override;
  void Fetch(fidl::Array<uint8_t> key, const FetchCallback& callback) override;
  void FetchPartial(fidl::Array<uint8_t> key,
                    int64_t offset,
                    int64_t max_size,
                    const FetchPartialCallback& callback) override;

  storage::PageStorage* page_storage_;
  std::unique_ptr<const storage::Commit> commit_;
  const std::string key_prefix_;
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_PAGE_SNAPSHOT_IMPL_H_
