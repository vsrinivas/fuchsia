// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_P2P_SYNC_IMPL_COMMIT_BATCH_H_
#define SRC_LEDGER_BIN_P2P_SYNC_IMPL_COMMIT_BATCH_H_

#include <lib/fit/function.h>

#include <list>
#include <string>
#include <vector>

#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/lib/fxl/strings/string_view.h"

namespace p2p_sync {

// CommitBatch holds all commits that should be added together in PageStorage.
class CommitBatch {
 public:
  // Delegate is used by |CommitBatch| to request missing commits.
  class Delegate {
   public:
    // Request missing commits from this batch. Commits will be added later
    // through CommitBatch::AddBatch.
    virtual void RequestCommits(fxl::StringView device,
                                std::vector<storage::CommitId> ids) = 0;
  };

  CommitBatch(std::string device, Delegate* delegate,
              storage::PageStorage* storage);
  CommitBatch(const CommitBatch&) = delete;
  const CommitBatch& operator=(const CommitBatch&) = delete;

  // Registers a callback to be called when the batch processing is completed,
  // either through success or an unrecoverable error. Part of the
  // callback::AutoCleanable* client API.
  void set_on_empty(fit::closure on_empty);

  // Adds the provided commits to this batch.
  // This method will attempt to add the whole batch to |PageStorage|, and may
  // request additional commits through the |Delegate| if needed.
  void AddToBatch(
      std::vector<storage::PageStorage::CommitIdAndBytes> new_commits);

 private:
  std::string const device_;
  Delegate* const delegate_;
  storage::PageStorage* const storage_;
  std::list<storage::PageStorage::CommitIdAndBytes> commits_;
  fit::closure on_empty_;

  // This must be the last member of the class.
  fxl::WeakPtrFactory<CommitBatch> weak_factory_;
};

}  // namespace p2p_sync

#endif  // SRC_LEDGER_BIN_P2P_SYNC_IMPL_COMMIT_BATCH_H_
