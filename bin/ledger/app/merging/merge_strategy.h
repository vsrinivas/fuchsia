// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_MERGING_MERGE_STRATEGY_H_
#define APPS_LEDGER_SRC_APP_MERGING_MERGE_STRATEGY_H_

#include <memory>

#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/ledger/src/callback/cancellable.h"
#include "apps/ledger/src/storage/public/commit.h"
#include "apps/ledger/src/storage/public/page_storage.h"

namespace ledger {
class PageManager;

// Interface for a merge algorithm.
class MergeStrategy {
 public:
  MergeStrategy() {}
  virtual ~MergeStrategy() {}

  // Sets a callback that will be called if this strategy is not to be used
  // anymore, for instance when the underlying merge mechanism is no longer
  // available.
  virtual void SetOnError(std::function<void()> on_error) = 0;

  virtual ftl::RefPtr<callback::Cancellable> Merge(
      storage::PageStorage* storage,
      PageManager* page_manager,
      std::unique_ptr<const storage::Commit> head_1,
      std::unique_ptr<const storage::Commit> head_2,
      std::unique_ptr<const storage::Commit> ancestor) = 0;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(MergeStrategy);
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_MERGING_MERGE_STRATEGY_H_
