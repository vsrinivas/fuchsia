// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/live_commit_tracker.h"

#include <iterator>

#include <lib/zx/time.h>

#include "src/ledger/bin/storage/public/types.h"

namespace storage {
void LiveCommitTracker::AddHeads(
    std::vector<std::pair<zx::time_utc, CommitId>> heads) {
  heads_.insert(std::make_move_iterator(heads.begin()),
                std::make_move_iterator(heads.end()));
}

void LiveCommitTracker::RemoveHeads(const std::vector<CommitId>& commit_ids) {
  for (const auto& commit_id : commit_ids) {
    auto it =
        std::find_if(heads_.begin(), heads_.end(),
                     [&commit_id](const std::pair<zx::time_utc, CommitId>& p) {
                       return p.second == commit_id;
                     });
    if (it != heads_.end()) {
      heads_.erase(it);
    }
  }
}

std::vector<CommitId> LiveCommitTracker::GetHeads() {
  auto result = std::vector<CommitId>();
  result.reserve(heads_.size());
  std::transform(
      heads_.begin(), heads_.end(), std::back_inserter(result),
      [](const auto& p) -> CommitId { return std::get<CommitId>(p); });
  return result;
}

}  // namespace storage
