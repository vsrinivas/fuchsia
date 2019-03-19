// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/live_commit_tracker.h"

#include <iterator>
#include <memory>
#include <tuple>

#include <lib/zx/time.h>

#include "src/ledger/bin/storage/public/commit.h"
#include "src/ledger/bin/storage/public/types.h"

namespace storage {
bool LiveCommitTracker::CommitComparator::operator()(
    const std::unique_ptr<const Commit>& left,
    const std::unique_ptr<const Commit>& right) const {
  return std::forward_as_tuple(left->GetTimestamp(), left->GetId()) <
         std::forward_as_tuple(right->GetTimestamp(), right->GetId());
}

void LiveCommitTracker::AddHeads(
    std::vector<std::unique_ptr<const Commit>> heads) {
  heads_.insert(std::make_move_iterator(heads.begin()),
                std::make_move_iterator(heads.end()));
}

void LiveCommitTracker::RemoveHeads(const std::vector<CommitId>& commit_ids) {
  for (const auto& commit_id : commit_ids) {
    auto it =
        std::find_if(heads_.begin(), heads_.end(),
                     [&commit_id](const std::unique_ptr<const Commit>& p) {
                       return p->GetId() == commit_id;
                     });
    if (it != heads_.end()) {
      heads_.erase(it);
    }
  }
}

std::vector<CommitId> LiveCommitTracker::GetHeads() {
  auto result = std::vector<CommitId>();
  result.reserve(heads_.size());
  std::transform(heads_.begin(), heads_.end(), std::back_inserter(result),
                 [](const auto& p) -> CommitId { return p->GetId(); });
  return result;
}

}  // namespace storage
