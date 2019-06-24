// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_LIVE_COMMIT_TRACKER_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_LIVE_COMMIT_TRACKER_H_

#include <lib/fit/function.h>
#include <lib/zx/time.h>

#include <memory>
#include <set>
#include <vector>

#include "lib/callback/auto_cleanable.h"
#include "peridot/lib/convert/convert.h"
#include "src/ledger/bin/storage/public/commit.h"
#include "src/ledger/bin/storage/public/types.h"

namespace storage {

// In-memory tracker for live commits.
class LiveCommitTracker {
 public:
  LiveCommitTracker() = default;
  // At destruction time, no live commits must remain, except the ones owned by
  // this very object.
  ~LiveCommitTracker();

  // Adds these commits to the list of current heads. In |GetHeads()| the heads
  // will be returned ordered by their timestamp, which is the |zx::time_utc|
  // element of each pair.
  void AddHeads(std::vector<std::unique_ptr<const Commit>> heads);

  // Removes these commits from the set of live heads.
  void RemoveHeads(const std::vector<CommitId>& commit_id);

  // Returns the current heads of a page, ordered by their associated time.
  std::vector<std::unique_ptr<const Commit>> GetHeads() const;

  // Registers a currently-untracked commit to be tracked.
  void RegisterCommit(Commit* commit);

  // Unregisters a currently tracked commit.
  void UnregisterCommit(Commit* commit);

  // Returns a copy of every currently live/tracked commit.
  std::vector<std::unique_ptr<const Commit>> GetLiveCommits();

 private:
  // CommitComparator orders commits by timestamp and ID.
  class CommitComparator {
    using is_transparent = std::true_type;

   public:
    bool operator()(const std::unique_ptr<const Commit>& left,
                    const std::unique_ptr<const Commit>& right) const;
    bool operator()(const Commit* left, const Commit* right) const;
  };

  // Set of currently live (in-memory) commits from the page tracked by this
  // object.
  std::set<const Commit*> live_commits_;
  // Set of the current heads of the page tracked by this object.
  std::set<std::unique_ptr<const Commit>, CommitComparator> heads_;
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_LIVE_COMMIT_TRACKER_H_
