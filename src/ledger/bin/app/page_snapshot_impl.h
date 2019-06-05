// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_PAGE_SNAPSHOT_IMPL_H_
#define SRC_LEDGER_BIN_APP_PAGE_SNAPSHOT_IMPL_H_

#include <memory>

#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/fidl/syncable.h"
#include "src/ledger/bin/storage/public/commit.h"
#include "src/ledger/bin/storage/public/page_storage.h"

namespace ledger {

// An implementation of the |PageSnapshot| FIDL interface.
class PageSnapshotImpl : public fuchsia::ledger::PageSnapshotSyncableDelegate {
 public:
  PageSnapshotImpl(storage::PageStorage* page_storage,
                   std::unique_ptr<const storage::Commit> commit,
                   std::string key_prefix);
  ~PageSnapshotImpl() override;

 private:
  // PageSnapshot:
  void GetEntries(
      std::vector<uint8_t> key_start, std::unique_ptr<Token> token,
      fit::function<void(Status, std::vector<Entry>, std::unique_ptr<Token>)>
          callback) override;
  void GetEntriesInline(std::vector<uint8_t> key_start,
                        std::unique_ptr<Token> token,
                        fit::function<void(Status, std::vector<InlinedEntry>,
                                           std::unique_ptr<Token>)>
                            callback) override;
  void GetKeys(std::vector<uint8_t> key_start, std::unique_ptr<Token> token,
               fit::function<void(Status, std::vector<std::vector<uint8_t>>,
                                  std::unique_ptr<Token>)>
                   callback) override;
  void Get(std::vector<uint8_t> key,
           fit::function<void(Status, fuchsia::ledger::PageSnapshot_Get_Result)>
               callback) override;
  void GetInline(std::vector<uint8_t> key,
                 fit::function<void(
                     Status, fuchsia::ledger::PageSnapshot_GetInline_Result)>
                     callback) override;
  void Fetch(
      std::vector<uint8_t> key,
      fit::function<void(Status, fuchsia::ledger::PageSnapshot_Fetch_Result)>
          callback) override;
  void FetchPartial(
      std::vector<uint8_t> key, int64_t offset, int64_t max_size,
      fit::function<void(Status,
                         fuchsia::ledger::PageSnapshot_FetchPartial_Result)>
          callback) override;

  storage::PageStorage* page_storage_;
  std::unique_ptr<const storage::Commit> commit_;
  const std::string key_prefix_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_PAGE_SNAPSHOT_IMPL_H_
