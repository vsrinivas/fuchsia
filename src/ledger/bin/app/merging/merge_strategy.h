// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_MERGING_MERGE_STRATEGY_H_
#define SRC_LEDGER_BIN_APP_MERGING_MERGE_STRATEGY_H_

#include <lib/fit/function.h>

#include <memory>

#include "src/ledger/bin/app/merging/merge_resolver.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/storage/public/commit.h"
#include "src/ledger/bin/storage/public/page_storage.h"

namespace ledger {
class ActivePageManager;

// Interface for a merge algorithm.
class MergeStrategy {
 public:
  MergeStrategy() = default;
  MergeStrategy(const MergeStrategy&) = delete;
  MergeStrategy& operator=(const MergeStrategy&) = delete;
  virtual ~MergeStrategy() = default;

  // Sets a callback that will be called if this strategy is not to be used
  // anymore, for instance when the underlying merge mechanism is no longer
  // available. This callback should not delete the strategy if there are merges
  // in progress.
  virtual void SetOnError(fit::function<void()> on_error) = 0;

  // Merge the given commits. MergeStrategy should not be deleted while merges
  // are in progress. The heads must be sorted according to their timestamps:
  // |storage::Commit::TimestampOrdered(head_1, head_2)| must be true.
  virtual void Merge(storage::PageStorage* storage, ActivePageManager* active_page_manager,
                     std::unique_ptr<const storage::Commit> head_1,
                     std::unique_ptr<const storage::Commit> head_2,
                     std::unique_ptr<const storage::Commit> ancestor,
                     fit::function<void(Status)> callback) = 0;

  // Cancel an in-progress merge. This must be called after |Merge| has been
  // called, and before the |on_done| callback.
  virtual void Cancel() = 0;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_MERGING_MERGE_STRATEGY_H_
