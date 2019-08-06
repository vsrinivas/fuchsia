// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_COMMIT_PRUNER_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_COMMIT_PRUNER_H_

#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/storage/impl/live_commit_tracker.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/coroutine/coroutine_manager.h"

namespace storage {

// Commit pruner computes which commits should be removed from the page storage.
class CommitPruner {
 public:
  CommitPruner(ledger::Environment* environment, PageStorage* storage,
               LiveCommitTracker* commit_tracker, CommitPruningPolicy policy);
  ~CommitPruner();

  // Performs a pruning cycle.
  void Prune(fit::function<void(Status)> callback);

 private:
  // Finds the latest unique common ancestor among the live commits, as given by the
  // LiveCommitTracker.
  Status FindLatestUniqueCommonAncestorSync(coroutine::CoroutineHandler* handler,
                                            std::unique_ptr<const storage::Commit>* result);
  // Returns all locally-known ancestors of a commit.
  Status GetAllAncestors(coroutine::CoroutineHandler* handler,
                         std::unique_ptr<const storage::Commit> base,
                         std::vector<std::unique_ptr<const storage::Commit>>* result);

  ledger::Environment* const environment_;
  PageStorage* const storage_;
  LiveCommitTracker* const commit_tracker_;

  // Policy for pruning commits. By default, we don't prune.
  CommitPruningPolicy const policy_;

  coroutine::CoroutineManager coroutine_manager_;
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_COMMIT_PRUNER_H_
