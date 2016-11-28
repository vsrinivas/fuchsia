// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_MERGING_DUMMY_MERGER_H_
#define APPS_LEDGER_SRC_APP_MERGING_DUMMY_MERGER_H_

#include <memory>
#include "apps/ledger/src/app/merging/merge_strategy.h"
#include "apps/ledger/src/storage/public/commit.h"
#include "lib/ftl/macros.h"

namespace ledger {
// Strategy for merging commits which does not merge.
class DummyMerger : public MergeStrategy {
 public:
  DummyMerger();
  ~DummyMerger() override;

  ftl::RefPtr<callback::Cancellable> Merge(
      std::unique_ptr<const storage::Commit> head_1,
      std::unique_ptr<const storage::Commit> head_2,
      std::unique_ptr<const storage::Commit> ancestor) override;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(DummyMerger);
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_MERGING_DUMMY_MERGER_H_
