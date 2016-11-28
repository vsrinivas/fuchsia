// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_MERGING_MERGE_STRATEGY_H_
#define APPS_LEDGER_SRC_APP_MERGING_MERGE_STRATEGY_H_

#include <memory>

#include "apps/ledger/src/callback/cancellable.h"
#include "apps/ledger/src/storage/public/commit.h"

namespace ledger {
// Interface for a merge algorithm.
class MergeStrategy {
 public:
  MergeStrategy() {}
  virtual ~MergeStrategy() {}

  virtual ftl::RefPtr<callback::Cancellable> Merge(
      std::unique_ptr<const storage::Commit> head_1,
      std::unique_ptr<const storage::Commit> head_2,
      std::unique_ptr<const storage::Commit> ancestor) = 0;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(MergeStrategy);
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_MERGING_MERGE_STRATEGY_H_
