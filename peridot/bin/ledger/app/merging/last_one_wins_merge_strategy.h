// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_MERGING_LAST_ONE_WINS_MERGE_STRATEGY_H_
#define PERIDOT_BIN_LEDGER_APP_MERGING_LAST_ONE_WINS_MERGE_STRATEGY_H_

#include <memory>

#include <lib/callback/auto_cleanable.h>
#include <lib/fit/function.h>
#include <lib/fxl/macros.h>

#include "peridot/bin/ledger/app/merging/merge_strategy.h"
#include "peridot/bin/ledger/storage/public/commit.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"

namespace ledger {
// Strategy for merging commits using a last-one-wins policy for conflicts.
// Commits are merged key-by-key. When a key has been modified on both sides,
// the value from the most recent commit is used.
class LastOneWinsMergeStrategy : public MergeStrategy {
 public:
  LastOneWinsMergeStrategy();
  ~LastOneWinsMergeStrategy() override;

  void SetOnError(fit::function<void()> on_error) override;

  void Merge(storage::PageStorage* storage, PageManager* page_manager,
             std::unique_ptr<const storage::Commit> head_1,
             std::unique_ptr<const storage::Commit> head_2,
             std::unique_ptr<const storage::Commit> ancestor,
             fit::function<void(Status)> callback) override;

  void Cancel() override;

 private:
  class LastOneWinsMerger;

  std::unique_ptr<LastOneWinsMerger> in_progress_merge_;
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_MERGING_LAST_ONE_WINS_MERGE_STRATEGY_H_
