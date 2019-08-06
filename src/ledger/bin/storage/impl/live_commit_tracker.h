// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_LIVE_COMMIT_TRACKER_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_LIVE_COMMIT_TRACKER_H_

#include <memory>
#include <vector>

#include "src/ledger/bin/storage/public/commit.h"

namespace storage {

// Interface for tracking live commits
class LiveCommitTracker {
 public:
  virtual ~LiveCommitTracker(){};

  // Adds these commits to the list of current heads. In |GetHeads()| the heads
  // will be returned ordered by their timestamp, which is the |zx::time_utc|
  // element of each pair.
  virtual void AddHeads(std::vector<std::unique_ptr<const Commit>> heads) = 0;

  // Removes these commits from the set of live heads.
  virtual void RemoveHeads(const std::vector<CommitId>& commit_id) = 0;

  // Returns the current heads of a page, ordered by their associated time.
  virtual std::vector<std::unique_ptr<const Commit>> GetHeads() const = 0;

  // Returns a copy of every currently live/tracked commit.
  virtual std::vector<std::unique_ptr<const Commit>> GetLiveCommits() const = 0;
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_LIVE_COMMIT_TRACKER_H_
