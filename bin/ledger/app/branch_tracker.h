// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_BRANCH_TRACKER_H_
#define APPS_LEDGER_SRC_APP_BRANCH_TRACKER_H_

#include "apps/ledger/services/ledger.fidl.h"
#include "apps/ledger/src/storage/public/commit_watcher.h"
#include "apps/ledger/src/storage/public/page_storage.h"

namespace ledger {

// Tracks the head of a commit "branch". A commit is chosen arbitrarily from the
// page head commits at construction. Subsequently, this object will track the
// head of this commit branch, unless reset by |SetBranchHead|. If two commits
// have the same parent, the first one to be received will be followed.
class BranchTracker : public storage::CommitWatcher {
 public:
  BranchTracker(storage::PageStorage* storage);
  ~BranchTracker();

  // Returns the head commit of the currently tracked branch.
  storage::CommitId GetBranchHeadId();

  // This method should be called by |PageImpl| when a journal is commited to
  // inform which branch should be tracked by the page and watchers from now on.
  void SetBranchHead(const storage::CommitId& commit_id);

 private:
  // storage::CommitWatcher:
  void OnNewCommit(const storage::Commit& commit,
                   storage::ChangeSource source) override;

  storage::PageStorage* storage_;
  storage::CommitId current_commit_;
  FTL_DISALLOW_COPY_AND_ASSIGN(BranchTracker);
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_BRANCH_TRACKER_H_
