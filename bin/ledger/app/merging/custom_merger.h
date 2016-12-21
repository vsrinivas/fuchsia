// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_MERGING_CUSTOM_MERGER_H_
#define APPS_LEDGER_SRC_APP_MERGING_CUSTOM_MERGER_H_

#include <memory>
#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/ledger/src/app/merging/merge_strategy.h"
#include "apps/ledger/src/storage/public/commit.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "lib/ftl/macros.h"

namespace ledger {
// Strategy for merging commits using the CUSTOM policy.
class CustomMerger : public MergeStrategy {
 public:
  explicit CustomMerger(ConflictResolverPtr conflict_resolver);
  ~CustomMerger() override;

  // MergeStrategy:
  void SetOnError(ftl::Closure on_error) override;

  ftl::RefPtr<callback::Cancellable> Merge(
      storage::PageStorage* storage,
      PageManager* page_manager,
      std::unique_ptr<const storage::Commit> head_1,
      std::unique_ptr<const storage::Commit> head_2,
      std::unique_ptr<const storage::Commit> ancestor) override;

 private:
  ftl::Closure on_error_;

  ConflictResolverPtr conflict_resolver_;

  FTL_DISALLOW_COPY_AND_ASSIGN(CustomMerger);
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_MERGING_CUSTOM_MERGER_H_
