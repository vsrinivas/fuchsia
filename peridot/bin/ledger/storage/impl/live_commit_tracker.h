// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_IMPL_LIVE_COMMIT_TRACKER_H_
#define PERIDOT_BIN_LEDGER_STORAGE_IMPL_LIVE_COMMIT_TRACKER_H_

#include <set>
#include <vector>

#include <lib/zx/time.h>

#include "peridot/bin/ledger/storage/public/types.h"
#include "peridot/lib/convert/convert.h"

namespace storage {
// In-memory tracker for live commits. Currently, this class only tracks the
// current heads of a page.
class LiveCommitTracker {
 public:
  LiveCommitTracker() = default;

  // Adds these commits to the list of current heads. In |GetHeads()| the heads
  // will be returned ordered by their timestamp, which is the |zx::time_utc|
  // element of each pair.
  void AddHeads(std::vector<std::pair<zx::time_utc, CommitId>> heads);

  // Removes these commits from the set of live heads.
  void RemoveHeads(const std::vector<CommitId>& commit_id);

  // Returns the current heads of a page, ordered by their associated time.
  std::vector<CommitId> GetHeads();

 private:
  std::set<std::pair<zx::time_utc, CommitId>> heads_;
};

}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_IMPL_LIVE_COMMIT_TRACKER_H_
