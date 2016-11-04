// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/branch_tracker.h"

namespace ledger {

BranchTracker::BranchTracker(storage::PageStorage* storage)
    : storage_(storage) {
  std::vector<storage::CommitId> commit_ids;
  // TODO(etiennej): Fail more nicely.
  FTL_CHECK(storage_->GetHeadCommitIds(&commit_ids) == storage::Status::OK);
  FTL_DCHECK(commit_ids.size() > 0);
  current_commit_ = commit_ids[0];
  storage_->AddCommitWatcher(this);
}

BranchTracker::~BranchTracker() {
  storage_->RemoveCommitWatcher(this);
}

storage::CommitId BranchTracker::GetBranchHeadId() {
  return current_commit_;
}
void BranchTracker::SetBranchHead(const storage::CommitId& commit_id) {
  current_commit_ = commit_id;
}

void BranchTracker::OnNewCommit(const storage::Commit& commit,
                                storage::ChangeSource source) {
  if (commit.GetId() == current_commit_) {
    return;
  }
  // This assumes commits are received in (partial) order.
  std::vector<storage::CommitId> parent_ids = commit.GetParentIds();
  if (std::find(parent_ids.begin(), parent_ids.end(), current_commit_) !=
      parent_ids.end()) {
    current_commit_ = commit.GetId();
  }
}
}  // namespace ledger
