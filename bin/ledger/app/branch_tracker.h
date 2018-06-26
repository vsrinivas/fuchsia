// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_BRANCH_TRACKER_H_
#define PERIDOT_BIN_LEDGER_APP_BRANCH_TRACKER_H_

#include <memory>

#include "lib/callback/auto_cleanable.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "peridot/bin/ledger/app/page_snapshot_impl.h"
#include "peridot/bin/ledger/coroutine/coroutine.h"
#include "peridot/bin/ledger/storage/public/commit_watcher.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"
#include "peridot/bin/ledger/storage/public/types.h"

namespace ledger {
class PageManager;

// Tracks the head of a commit "branch". A commit is chosen arbitrarily from the
// page's head commits at construction. Subsequently, this object will track the
// head of this commit branch, unless reset by |SetBranchHead|. If two commits
// have the same parent, the first one to be received will be tracked.
class BranchTracker : public storage::CommitWatcher {
 public:
  BranchTracker(coroutine::CoroutineService* coroutine_service,
                PageManager* manager, storage::PageStorage* storage);
  ~BranchTracker() override;

  void Init(std::function<void(Status)> on_done);

  void set_on_empty(fxl::Closure on_empty_callback);

  // Returns the head commit of the currently tracked branch.
  const storage::CommitId& GetBranchHeadId();

  // Registers a new PageWatcher interface.
  void RegisterPageWatcher(PageWatcherPtr page_watcher_ptr,
                           std::unique_ptr<const storage::Commit> base_commit,
                           std::string key_prefix);

  // Informs the BranchTracker that a transaction is in progress. It first
  // drains all pending Watcher updates, then stops sending them until
  // |StopTransaction| is called. |watchers_drained_callback| is called when all
  // watcher updates have been processed by the clients. This should be used by
  // |PageDelegate| when a transaction is in progress.
  void StartTransaction(fxl::Closure watchers_drained_callback);

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

  void InitCommitAndSetWatcher(storage::CommitId commit_id);

  void CheckEmpty();

  coroutine::CoroutineService* coroutine_service_;
  PageManager* manager_;
  storage::PageStorage* storage_;
  callback::AutoCleanableSet<PageWatcherContainer> watchers_;
  fxl::Closure on_empty_callback_;

  bool transaction_in_progress_;
  // The following two variables hold the commit object and id correspondingly
  // of the commit tracked by this BranchTracker. |current_commit_| is used
  // for notifying the watchers. On initialization, |current_commit_id_| is set
  // to track the first head as returned from PageStorage. |current_commit_| at
  // that point equals nullptr and is only updated with a valid Commit,
  // corresponding to the tracked id, after the first call to OnNewCommits or
  // StopTransaction. Since the notifications are sent to the watchers only
  // after updating the tracked commit, the value of the |current_commit_| at
  // initialization (which is set to nullptr) is not necessary.
  std::unique_ptr<const storage::Commit> current_commit_;
  storage::CommitId current_commit_id_;

  // This must be the last member of the class.
  fxl::WeakPtrFactory<BranchTracker> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BranchTracker);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_BRANCH_TRACKER_H_
