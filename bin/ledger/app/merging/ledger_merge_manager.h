// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_MERGING_LEDGER_MERGE_MANAGER_H_
#define APPS_LEDGER_SRC_APP_MERGING_LEDGER_MERGE_MANAGER_H_

#include <memory>
#include "apps/ledger/src/app/merging/merge_resolver.h"
#include "apps/ledger/src/callback/auto_cleanable.h"
#include "apps/ledger/src/storage/public/commit.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "lib/ftl/macros.h"

namespace ledger {
class LedgerMergeManager {
 public:
  LedgerMergeManager() {}
  ~LedgerMergeManager() {}

  std::unique_ptr<MergeResolver> GetMergeResolver(
      storage::PageStorage* storage);

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(LedgerMergeManager);
};
}
#endif  // APPS_LEDGER_SRC_APP_MERGING_LEDGER_MERGE_MANAGER_H_
