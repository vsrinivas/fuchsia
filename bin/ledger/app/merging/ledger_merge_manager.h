// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_MERGING_LEDGER_MERGE_MANAGER_H_
#define APPS_LEDGER_SRC_APP_MERGING_LEDGER_MERGE_MANAGER_H_

#include <memory>
#include <unordered_map>
#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/ledger/src/app/merging/merge_resolver.h"
#include "apps/ledger/src/callback/auto_cleanable.h"
#include "apps/ledger/src/storage/public/commit.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "lib/fxl/macros.h"

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
      std::function<void(std::unique_ptr<MergeStrategy>)> strategy_callback);
  void ResetStrategyForPage(storage::PageId page_id);

  Environment* const environment_;
  ConflictResolverFactoryPtr conflict_resolver_factory_;

  std::unordered_map<storage::PageId, MergeResolver*> resolvers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerMergeManager);
};
}  // namespace ledger
#endif  // APPS_LEDGER_SRC_APP_MERGING_LEDGER_MERGE_MANAGER_H_
