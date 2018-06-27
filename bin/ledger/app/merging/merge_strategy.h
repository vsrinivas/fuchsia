// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_MERGING_MERGE_STRATEGY_H_
#define PERIDOT_BIN_LEDGER_APP_MERGING_MERGE_STRATEGY_H_

#include <memory>

#include <lib/fit/function.h>

#include "peridot/bin/ledger/app/merging/merge_resolver.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/storage/public/commit.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"

namespace ledger {
class PageManager;

// Interface for a merge algorithm.
class MergeStrategy {
 public:
  MergeStrategy() {}
  virtual ~MergeStrategy() {}

  // Sets a callback that will be called if this strategy is not to be used
  // anymore, for instance when the underlying merge mechanism is no longer
  // available. This callback should not delete the strategy if there are merges
  // in progress.
  virtual void SetOnError(fit::function<void()> on_error) = 0;

  // Merge the given commits. head_1.timesteamp must be less or equals to
  // head_2.timestamp. MergeStrategy should not be deleted while merges are in
  // progress.
  virtual void Merge(storage::PageStorage* storage, PageManager* page_manager,
                     std::unique_ptr<const storage::Commit> head_1,
                     std::unique_ptr<const storage::Commit> head_2,
                     std::unique_ptr<const storage::Commit> ancestor,
                     fit::function<void(Status)> callback) = 0;

  // Cancel an in-progress merge. This must be called after |Merge| has been
  // called, and before the |on_done| callback.
  virtual void Cancel() = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(MergeStrategy);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_MERGING_MERGE_STRATEGY_H_
