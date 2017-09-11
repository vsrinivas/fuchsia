// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_MERGING_CUSTOM_MERGE_STRATEGY_H_
#define APPS_LEDGER_SRC_APP_MERGING_CUSTOM_MERGE_STRATEGY_H_

#include <memory>
#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/ledger/src/app/merging/conflict_resolver_client.h"
#include "apps/ledger/src/app/merging/merge_strategy.h"
#include "apps/ledger/src/storage/public/commit.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "lib/fxl/macros.h"

namespace ledger {
// Strategy for merging commits using the CUSTOM policy.
class CustomMergeStrategy : public MergeStrategy {
 public:
  explicit CustomMergeStrategy(ConflictResolverPtr conflict_resolver);
  ~CustomMergeStrategy() override;

  // MergeStrategy:
  void SetOnError(fxl::Closure on_error) override;

  void Merge(storage::PageStorage* storage,
             PageManager* page_manager,
             std::unique_ptr<const storage::Commit> head_1,
             std::unique_ptr<const storage::Commit> head_2,
             std::unique_ptr<const storage::Commit> ancestor,
             std::function<void(Status)> callback) override;

  void Cancel() override;

 private:
  fxl::Closure on_error_;

  ConflictResolverPtr conflict_resolver_;

  std::unique_ptr<ConflictResolverClient> in_progress_merge_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CustomMergeStrategy);
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_MERGING_CUSTOM_MERGE_STRATEGY_H_
