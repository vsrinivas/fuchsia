// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_MERGING_AUTO_MERGE_STRATEGY_H_
#define SRC_LEDGER_BIN_APP_MERGING_AUTO_MERGE_STRATEGY_H_

#include <lib/fit/function.h>

#include <memory>

#include "src/ledger/bin/app/merging/merge_strategy.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/storage/public/commit.h"
#include "src/ledger/bin/storage/public/page_storage.h"

namespace ledger {
// Strategy for merging commits using the AUTOMATIC_WITH_FALLBACK policy.
class AutoMergeStrategy : public MergeStrategy {
 public:
  explicit AutoMergeStrategy(ConflictResolverPtr conflict_resolver);
  AutoMergeStrategy(const AutoMergeStrategy&) = delete;
  AutoMergeStrategy& operator=(const AutoMergeStrategy&) = delete;
  ~AutoMergeStrategy() override;

  // MergeStrategy:
  void SetOnError(fit::closure on_error) override;

  void Merge(storage::PageStorage* storage, ActivePageManager* active_page_manager,
             std::unique_ptr<const storage::Commit> head_1,
             std::unique_ptr<const storage::Commit> head_2,
             std::unique_ptr<const storage::Commit> ancestor,
             fit::function<void(Status)> callback) override;

  void Cancel() override;

 private:
  class AutoMerger;

  fit::closure on_error_;

  ConflictResolverPtr conflict_resolver_;

  std::unique_ptr<AutoMerger> in_progress_merge_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_MERGING_AUTO_MERGE_STRATEGY_H_
