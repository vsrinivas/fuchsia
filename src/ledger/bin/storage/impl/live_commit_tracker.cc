// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/live_commit_tracker.h"

#include <lib/fit/function.h>
#include <lib/zx/time.h>

#include <algorithm>
#include <iterator>
#include <memory>
#include <tuple>

#include "src/ledger/bin/storage/public/commit.h"
#include "src/ledger/bin/storage/public/types.h"

namespace storage {
bool LiveCommitTracker::CommitComparator::operator()(
    const std::unique_ptr<const Commit>& left, const std::unique_ptr<const Commit>& right) const {
  return operator()(left.get(), right.get());
}

bool LiveCommitTracker::CommitComparator::operator()(const Commit* left,
                                                     const Commit* right) const {
  return std::forward_as_tuple(left->GetTimestamp(), left->GetId()) <
         std::forward_as_tuple(right->GetTimestamp(), right->GetId());
}

LiveCommitTracker::~LiveCommitTracker() {
  // The only live commits in this object at destruction time must be head
  // commits.
  FXL_DCHECK(std::all_of(live_commits_.begin(), live_commits_.end(), [this](const Commit* commit) {
    return std::find_if(heads_.begin(), heads_.end(),
                        [commit](const std::unique_ptr<const Commit>& element) {
                          return element.get() == commit;
                        }) != heads_.end();
  }));
}

void LiveCommitTracker::AddHeads(std::vector<std::unique_ptr<const Commit>> heads) {
  heads_.insert(std::make_move_iterator(heads.begin()), std::make_move_iterator(heads.end()));
}

void LiveCommitTracker::RemoveHeads(const std::vector<CommitId>& commit_ids) {
  for (const auto& commit_id : commit_ids) {
    auto it = std::find_if(
        heads_.begin(), heads_.end(),
        [&commit_id](const std::unique_ptr<const Commit>& p) { return p->GetId() == commit_id; });
    if (it != heads_.end()) {
      heads_.erase(it);
    }
  }
}

std::vector<std::unique_ptr<const Commit>> LiveCommitTracker::GetHeads() const {
  auto result = std::vector<std::unique_ptr<const Commit>>();
  result.reserve(heads_.size());
  std::transform(heads_.begin(), heads_.end(), std::back_inserter(result),
                 [](const auto& p) -> std::unique_ptr<const Commit> { return p->Clone(); });
  return result;
}

void LiveCommitTracker::RegisterCommit(Commit* commit) {
  auto result = live_commits_.insert(commit);
  // Verifies that this is indeed a new commit, and not an already known commit.
  FXL_DCHECK(result.second);
}

void LiveCommitTracker::UnregisterCommit(Commit* commit) {
  auto erased = live_commits_.erase(commit);
  // Verifies that the commit was registered previously.
  FXL_DCHECK(erased != 0);
}

std::vector<std::unique_ptr<const Commit>> LiveCommitTracker::GetLiveCommits() {
  // This deduplicates identical commits.
  std::set<const Commit*, CommitComparator> live(live_commits_.begin(), live_commits_.end());
  std::vector<std::unique_ptr<const Commit>> commits;
  for (const Commit* commit : live) {
    commits.emplace_back(commit->Clone());
  }
  return commits;
}

}  // namespace storage
