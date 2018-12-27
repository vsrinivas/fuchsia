// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_PAGE_SNAPSHOT_IMPL_H_
#define PERIDOT_BIN_LEDGER_APP_PAGE_SNAPSHOT_IMPL_H_

#include <memory>

#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/storage/public/commit.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"

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
  void GetEntries(std::vector<uint8_t> key_start,
                  std::unique_ptr<Token> token,
                  GetEntriesCallback callback) override;
  void GetEntriesInline(std::vector<uint8_t> key_start,
                        std::unique_ptr<Token> token,
                        GetEntriesInlineCallback callback) override;
  void GetKeys(std::vector<uint8_t> key_start, std::unique_ptr<Token> token,
               GetKeysCallback callback) override;
  void Get(std::vector<uint8_t> key, GetCallback callback) override;
  void GetInline(std::vector<uint8_t> key,
                 GetInlineCallback callback) override;
  void Fetch(std::vector<uint8_t> key, FetchCallback callback) override;
  void FetchPartial(std::vector<uint8_t> key, int64_t offset,
                    int64_t max_size, FetchPartialCallback callback) override;

  storage::PageStorage* page_storage_;
  std::unique_ptr<const storage::Commit> commit_;
  const std::string key_prefix_;
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_PAGE_SNAPSHOT_IMPL_H_
