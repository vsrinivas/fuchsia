// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/merging/ledger_merge_manager.h"

#include <memory>
#include <string>

#include "peridot/bin/ledger/app/merging/auto_merge_strategy.h"
#include "peridot/bin/ledger/app/merging/custom_merge_strategy.h"
#include "peridot/bin/ledger/app/merging/last_one_wins_merge_strategy.h"
#include "peridot/bin/ledger/app/merging/merge_resolver.h"
#include "peridot/bin/ledger/backoff/exponential_backoff.h"
#include "peridot/bin/ledger/glue/crypto/rand.h"

namespace ledger {
LedgerMergeManager::LedgerMergeManager(Environment* environment)
    : environment_(environment) {}

LedgerMergeManager::~LedgerMergeManager() {}

void LedgerMergeManager::SetFactory(
    fidl::InterfaceHandle<ConflictResolverFactory> factory) {
  conflict_resolver_factory_ =
      ConflictResolverFactoryPtr::Create(std::move(factory));
  for (const auto& item : resolvers_) {
    item.second->SetMergeStrategy(nullptr);
    GetResolverStrategyForPage(item.first, [
      this, page_id = item.first
    ](std::unique_ptr<MergeStrategy> strategy) mutable {
      if (resolvers_.find(page_id) != resolvers_.end()) {
        resolvers_[page_id]->SetMergeStrategy(std::move(strategy));
      }
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
      [this, page_id]() { RemoveResolver(page_id); }, environment_, storage,
      std::make_unique<backoff::ExponentialBackoff>(
          fxl::TimeDelta::FromMilliseconds(10), 2u,
          fxl::TimeDelta::FromSeconds(60 * 60), glue::RandUint64));
  resolvers_[page_id] = resolver.get();
  GetResolverStrategyForPage(
      page_id,
      [this, page_id](std::unique_ptr<MergeStrategy> strategy) mutable {
        if (resolvers_.find(page_id) != resolvers_.end()) {
          resolvers_[page_id]->SetMergeStrategy(std::move(strategy));
        }
      });
  return resolver;
}

void LedgerMergeManager::GetResolverStrategyForPage(
    const storage::PageId& page_id,
    std::function<void(std::unique_ptr<MergeStrategy>)> strategy_callback) {
  if (!conflict_resolver_factory_) {
    strategy_callback(std::make_unique<LastOneWinsMergeStrategy>());
  } else if (conflict_resolver_factory_.encountered_error()) {
    strategy_callback(nullptr);
  } else {
    conflict_resolver_factory_->GetPolicy(
        convert::ToArray(page_id),
        [this, page_id, strategy_callback](MergePolicy policy) {
          switch (policy) {
            case MergePolicy::NONE:
              strategy_callback(nullptr);
              break;
            case MergePolicy::LAST_ONE_WINS:
              strategy_callback(std::make_unique<LastOneWinsMergeStrategy>());
              break;
            case MergePolicy::AUTOMATIC_WITH_FALLBACK: {
              ConflictResolverPtr conflict_resolver;
              conflict_resolver_factory_->NewConflictResolver(
                  convert::ToArray(page_id), conflict_resolver.NewRequest());
              std::unique_ptr<AutoMergeStrategy> auto_merge_strategy =
                  std::make_unique<AutoMergeStrategy>(
                      std::move(conflict_resolver));
              auto_merge_strategy->SetOnError(
                  [this, page_id]() { ResetStrategyForPage(page_id); });
              strategy_callback(std::move(auto_merge_strategy));
              break;
            }
            case MergePolicy::CUSTOM: {
              ConflictResolverPtr conflict_resolver;
              conflict_resolver_factory_->NewConflictResolver(
                  convert::ToArray(page_id), conflict_resolver.NewRequest());
              std::unique_ptr<CustomMergeStrategy> custom_merge_strategy =
                  std::make_unique<CustomMergeStrategy>(
                      std::move(conflict_resolver));
              custom_merge_strategy->SetOnError(
                  [this, page_id]() { ResetStrategyForPage(page_id); });
              strategy_callback(std::move(custom_merge_strategy));
              break;
            }
          }
        });
  }
}

void LedgerMergeManager::ResetStrategyForPage(storage::PageId page_id) {
  if (resolvers_.find(page_id) == resolvers_.end()) {
    return;
  }
  resolvers_[page_id]->SetMergeStrategy(nullptr);
  GetResolverStrategyForPage(
      page_id,
      [this, page_id](std::unique_ptr<MergeStrategy> strategy) mutable {
        if (resolvers_.find(page_id) != resolvers_.end()) {
          resolvers_[page_id]->SetMergeStrategy(std::move(strategy));
        }
      });
}
}  // namespace ledger
