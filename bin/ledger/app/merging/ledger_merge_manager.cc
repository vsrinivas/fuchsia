// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/merging/ledger_merge_manager.h"

#include <memory>

#include "apps/ledger/src/app/merging/last_one_wins_merger.h"
#include "apps/ledger/src/app/merging/merge_resolver.h"

namespace ledger {
void LedgerMergeManager::SetFactory(
    fidl::InterfaceHandle<ConflictResolverFactory> factory) {
  conflict_resolver_factory_ =
      ConflictResolverFactoryPtr::Create(std::move(factory));
  for (const auto& item : resolvers_) {
    item.second->SetMergeStrategy(nullptr);
    GetResolverStrategyForPage(
        item.first, [resolver_ptr = item.second](
                        std::unique_ptr<MergeStrategy> strategy) mutable {
          resolver_ptr->SetMergeStrategy(std::move(strategy));
        });
  }
}

void LedgerMergeManager::RemoveResolver(const storage::PageId& page_id) {
  resolvers_.erase(page_id);
}

std::unique_ptr<MergeResolver> LedgerMergeManager::GetMergeResolver(
    storage::PageStorage* storage) {
  storage::PageId page_id = storage->GetId();
  std::unique_ptr<MergeResolver> resolver = std::make_unique<MergeResolver>(
      [this, page_id]() { RemoveResolver(page_id); }, storage);
  resolvers_[page_id] = resolver.get();
  GetResolverStrategyForPage(
      page_id, [resolver_ptr = resolver.get()](
                   std::unique_ptr<MergeStrategy> strategy) mutable {
        resolver_ptr->SetMergeStrategy(std::move(strategy));
      });
  return resolver;
}

void LedgerMergeManager::GetResolverStrategyForPage(
    const storage::PageId& page_id,
    std::function<void(std::unique_ptr<MergeStrategy>)> strategy_callback) {
  if (!conflict_resolver_factory_) {
    strategy_callback(std::make_unique<LastOneWinsMerger>());
  } else {
    conflict_resolver_factory_->GetPolicy(
        convert::ToArray(page_id),
        [callback = std::move(strategy_callback)](MergePolicy policy) {
          switch (policy) {
            case MergePolicy::NONE:
              callback(nullptr);
              break;
            case MergePolicy::LAST_ONE_WINS:
              callback(std::make_unique<LastOneWinsMerger>());
              break;
            case MergePolicy::AUTOMATIC_WITH_FALLBACK:
              // TODO(etiennej): see bug LE-124.
              FTL_LOG(ERROR) << "AUTOMATIC_WITH_FALLBACK merge policy not "
                                "implemented, defaulting to NONE";
              break;
            case MergePolicy::CUSTOM:
              // TODO(etiennej): see bug LE-123.
              FTL_LOG(ERROR)
                  << "CUSTOM merge policy not implemented, defaulting to NONE";
              break;
          }
        });
  }
}
}  // namespace ledger
