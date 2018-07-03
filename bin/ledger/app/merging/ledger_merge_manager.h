// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_MERGING_LEDGER_MERGE_MANAGER_H_
#define PERIDOT_BIN_LEDGER_APP_MERGING_LEDGER_MERGE_MANAGER_H_

#include <map>
#include <memory>

#include <lib/callback/auto_cleanable.h>
#include <lib/fit/function.h>
#include <lib/fxl/macros.h>

#include "peridot/bin/ledger/app/merging/merge_resolver.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/storage/public/commit.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"

namespace ledger {
// Manages the strategies for handling merges and conflicts for a ledger as
// managed by |LedgerManager|.
// Holds a ConflictResolverFactory if the client provides one.
// |LedgerMergeManager| must outlive all MergeResolver it provides.
class LedgerMergeManager {
 public:
  explicit LedgerMergeManager(Environment* environment);
  ~LedgerMergeManager();

  void SetFactory(fidl::InterfaceHandle<ConflictResolverFactory> factory);

  std::unique_ptr<MergeResolver> GetMergeResolver(
      storage::PageStorage* storage);

 private:
  void RemoveResolver(const storage::PageId& page_id);
  void GetResolverStrategyForPage(
      const storage::PageId& page_id,
      fit::function<void(std::unique_ptr<MergeStrategy>)> strategy_callback);
  void ResetStrategyForPage(storage::PageId page_id);

  Environment* const environment_;
  ConflictResolverFactoryPtr conflict_resolver_factory_;

  std::map<storage::PageId, MergeResolver*> resolvers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerMergeManager);
};
}  // namespace ledger
#endif  // PERIDOT_BIN_LEDGER_APP_MERGING_LEDGER_MERGE_MANAGER_H_
