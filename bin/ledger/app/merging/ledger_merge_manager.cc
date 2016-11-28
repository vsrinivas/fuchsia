// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/merging/ledger_merge_manager.h"

#include <memory>

#include "apps/ledger/src/app/merging/last_one_wins_merger.h"
#include "apps/ledger/src/app/merging/merge_resolver.h"
namespace ledger {

std::unique_ptr<MergeResolver> LedgerMergeManager::GetMergeResolver(
    storage::PageStorage* storage) {
  std::unique_ptr<LastOneWinsMerger> strategy =
      std::make_unique<LastOneWinsMerger>(storage);
  return std::make_unique<MergeResolver>(storage, std::move(strategy));
}

}  // namespace ledger
