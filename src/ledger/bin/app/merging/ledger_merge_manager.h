// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_MERGING_LEDGER_MERGE_MANAGER_H_
#define SRC_LEDGER_BIN_APP_MERGING_LEDGER_MERGE_MANAGER_H_

#include <lib/fidl/cpp/interface_ptr.h>
#include <lib/fit/function.h>

#include <map>
#include <memory>

#include "src/ledger/bin/app/merging/merge_resolver.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/storage/public/commit.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/lib/callback/auto_cleanable.h"

namespace ledger {

// Manages the strategies for handling merges and conflicts for a ledger as
// managed by |LedgerManager|.
// Holds a ConflictResolverFactory if the client provides one.
// |LedgerMergeManager| must outlive all MergeResolver it provides.
class LedgerMergeManager {
 public:
  explicit LedgerMergeManager(Environment* environment);
  LedgerMergeManager(const LedgerMergeManager&) = delete;
  LedgerMergeManager& operator=(const LedgerMergeManager&) = delete;
  ~LedgerMergeManager();

  void AddFactory(fidl::InterfaceHandle<ConflictResolverFactory> factory);

  std::unique_ptr<MergeResolver> GetMergeResolver(storage::PageStorage* storage);

 private:
  void ResetFactory();
  void RemoveResolver(const storage::PageId& page_id);
  void GetResolverStrategyForPage(
      const storage::PageId& page_id,
      fit::function<void(std::unique_ptr<MergeStrategy>)> strategy_callback);
  void ResetStrategyForPage(storage::PageId page_id);

  Environment* const environment_;

  class ConflictResolverFactoryPtrContainer;

  // Inactive, available conflict resolver factories
  AutoCleanableSet<ConflictResolverFactoryPtrContainer> conflict_resolver_factories_;
  // The ConflictResolverFactory that is currently in use
  fidl::InterfacePtr<ConflictResolverFactory> current_conflict_resolver_factory_;
  // |true| if using the default last-one-wins conflict resolver factory
  bool using_default_conflict_resolver_ = true;

  std::map<storage::PageId, MergeResolver*> resolvers_;
};
}  // namespace ledger
#endif  // SRC_LEDGER_BIN_APP_MERGING_LEDGER_MERGE_MANAGER_H_
