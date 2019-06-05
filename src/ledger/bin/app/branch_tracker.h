// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_BRANCH_TRACKER_H_
#define SRC_LEDGER_BIN_APP_BRANCH_TRACKER_H_

#include <lib/callback/auto_cleanable.h>
#include <lib/fit/function.h>

#include <memory>

#include "src/ledger/bin/app/page_snapshot_impl.h"
#include "src/ledger/bin/storage/public/commit_watcher.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/coroutine/coroutine.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace ledger {
class ActivePageManager;

// Tracks the head of a commit "branch". A commit is chosen arbitrarily from the
// page's head commits at construction. Subsequently, this object will track the
// head of this commit branch, unless reset by |SetBranchHead|. If two commits
// have the same parent, the first one to be received will be tracked.
class BranchTracker : public storage::CommitWatcher {
 public:
  BranchTracker(coroutine::CoroutineService* coroutine_service,
                ActivePageManager* manager, storage::PageStorage* storage);
  ~BranchTracker() override;

  Status Init();

  void set_on_empty(fit::closure on_empty_callback);

  // Returns the head commit of the currently tracked branch.
  std::unique_ptr<const storage::Commit> GetBranchHead();

  // Registers a new PageWatcher interface.
  void RegisterPageWatcher(PageWatcherPtr page_watcher_ptr,
                           std::unique_ptr<const storage::Commit> base_commit,
                           std::string key_prefix);

  // Informs the BranchTracker that a transaction is in progress. It first
  // drains all pending Watcher updates, then stops sending them until
  // |StopTransaction| is called. |watchers_drained_callback| is called when all
  // watcher updates have been processed by the clients. This should be used by
  // |PageDelegate| when a transaction is in progress.
  void StartTransaction(fit::closure watchers_drained_callback);

  // Informs the BranchTracker that a transaction is no longer in progress.
  // Resumes sending updates to registered watchers. This should be used by
  // |PageDelegate| when a transaction is committed or rolled back.
  // |commit| must be the one created by the transaction if it was committed, or
  // nullptr otherwise.
  void StopTransaction(std::unique_ptr<const storage::Commit> commit);

  // Returns true if there are no watchers registered.
  bool IsEmpty();

 private:
  class PageWatcherContainer;

  // storage::CommitWatcher:
  void OnNewCommits(
      const std::vector<std::unique_ptr<const storage::Commit>>& commits,
      storage::ChangeSource source) override;

  void CheckEmpty();

  coroutine::CoroutineService* coroutine_service_;
  ActivePageManager* manager_;
  storage::PageStorage* storage_;
  callback::AutoCleanableSet<PageWatcherContainer> watchers_;
  fit::closure on_empty_callback_;

  bool transaction_in_progress_;
  std::unique_ptr<const storage::Commit> current_commit_;

  // This must be the last member of the class.
  fxl::WeakPtrFactory<BranchTracker> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BranchTracker);
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_BRANCH_TRACKER_H_
