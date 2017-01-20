// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_BRANCH_TRACKER_H_
#define APPS_LEDGER_SRC_APP_BRANCH_TRACKER_H_

#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/ledger/src/app/fidl/bound_interface.h"
#include "apps/ledger/src/app/page_impl.h"
#include "apps/ledger/src/app/page_snapshot_impl.h"
#include "apps/ledger/src/callback/auto_cleanable.h"
#include "apps/ledger/src/storage/public/commit_watcher.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "lib/fidl/cpp/bindings/binding.h"

namespace ledger {
class PageManager;

// Tracks the head of a commit "branch". A commit is chosen arbitrarily from the
// page head commits at construction. Subsequently, this object will track the
// head of this commit branch, unless reset by |SetBranchHead|. If two commits
// have the same parent, the first one to be received will be followed.
class BranchTracker : public storage::CommitWatcher {
 public:
  BranchTracker(PageManager* manager,
                storage::PageStorage* storage,
                fidl::InterfaceRequest<Page> request);
  ~BranchTracker();

  void set_on_empty(ftl::Closure on_empty_callback);

  // Returns the head commit of the currently tracked branch.
  const storage::CommitId& GetBranchHeadId();

  // Registers a new PageWatcher interface.
  void RegisterPageWatcher(PageWatcherPtr page_watcher_ptr,
                           std::unique_ptr<const storage::Commit> base_commit);

  // This method should be called by |PageImpl| when a journal is commited to
  // inform which branch should be tracked by the page and watchers from now on.
  void SetBranchHead(const storage::CommitId& commit_id);

  // Informs the BranchTracker that a transaction is in progress. It first
  // drains all pending Watcher updates, then stop sending them until
  // |StopTransaction| is called. |watchers_drained_callback| is called when all
  // watcher updates have been processed by the clients. This should be used by
  // |PageImpl| when a transaction is in progress.
  void StartTransaction(ftl::Closure watchers_drained_callback);

  // Informs the BranchTracker that a transaction is no longer in progress.
  // Resumes sending updates to registered watchers. This should be used by
  // |PageImpl| when a transaction is committed or rolled back.
  void StopTransaction();

 private:
  class PageWatcherContainer;

  // storage::CommitWatcher:
  void OnNewCommits(
      const std::vector<std::unique_ptr<const storage::Commit>>& commits,
      storage::ChangeSource source) override;

  void CheckEmpty();

  PageManager* manager_;
  storage::PageStorage* storage_;
  BoundInterface<Page, PageImpl> interface_;
  callback::AutoCleanableSet<PageWatcherContainer> watchers_;
  ftl::Closure on_empty_callback_;

  bool transaction_in_progress_;
  storage::CommitId current_commit_;
  FTL_DISALLOW_COPY_AND_ASSIGN(BranchTracker);
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_BRANCH_TRACKER_H_
