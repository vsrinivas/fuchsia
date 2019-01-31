// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_MERGING_CUSTOM_MERGE_STRATEGY_H_
#define PERIDOT_BIN_LEDGER_APP_MERGING_CUSTOM_MERGE_STRATEGY_H_

#include <memory>

#include <lib/fit/function.h>
#include <lib/fxl/macros.h>

#include "peridot/bin/ledger/app/merging/conflict_resolver_client.h"
#include "peridot/bin/ledger/app/merging/merge_strategy.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/storage/public/commit.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"

namespace ledger {
// Strategy for merging commits using the CUSTOM policy.
class CustomMergeStrategy : public MergeStrategy {
 public:
  explicit CustomMergeStrategy(ConflictResolverPtr conflict_resolver);
  ~CustomMergeStrategy() override;

  // MergeStrategy:
  void SetOnError(fit::closure on_error) override;

  void Merge(storage::PageStorage* storage, PageManager* page_manager,
             std::unique_ptr<const storage::Commit> head_1,
             std::unique_ptr<const storage::Commit> head_2,
             std::unique_ptr<const storage::Commit> ancestor,
             fit::function<void(Status)> callback) override;

  void Cancel() override;

 private:
  fit::closure on_error_;

  ConflictResolverPtr conflict_resolver_;

  std::unique_ptr<ConflictResolverClient> in_progress_merge_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CustomMergeStrategy);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_MERGING_CUSTOM_MERGE_STRATEGY_H_
