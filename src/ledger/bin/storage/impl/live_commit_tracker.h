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

  // Returns a copy of every currently live/tracked commit.
  virtual std::vector<std::unique_ptr<const Commit>> GetLiveCommits() const = 0;
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_LIVE_COMMIT_TRACKER_H_
