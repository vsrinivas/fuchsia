// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_LIVE_COMMIT_TRACKER_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_LIVE_COMMIT_TRACKER_H_

#include <lib/zx/time.h>

#include <memory>
#include <set>
#include <vector>

#include "peridot/lib/convert/convert.h"
#include "src/ledger/bin/storage/public/commit.h"
#include "src/ledger/bin/storage/public/types.h"

namespace storage {

// In-memory tracker for live commits. Currently, this class only tracks the
// current heads of a page.
class LiveCommitTracker {
 public:
  LiveCommitTracker() = default;

  // Adds these commits to the list of current heads. In |GetHeads()| the heads
  // will be returned ordered by their timestamp, which is the |zx::time_utc|
  // element of each pair.
  void AddHeads(std::vector<std::unique_ptr<const Commit>> heads);

  // Removes these commits from the set of live heads.
  void RemoveHeads(const std::vector<CommitId>& commit_id);

  // Returns the current heads of a page, ordered by their associated time.
  std::vector<std::unique_ptr<const Commit>> GetHeads();

 private:
  class CommitComparator {
   public:
    bool operator()(const std::unique_ptr<const Commit>& left,
                    const std::unique_ptr<const Commit>& right) const;
  };

  std::set<std::unique_ptr<const Commit>, CommitComparator> heads_;
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_LIVE_COMMIT_TRACKER_H_
