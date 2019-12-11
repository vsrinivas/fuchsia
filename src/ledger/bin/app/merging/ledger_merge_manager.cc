// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/merging/ledger_merge_manager.h"

#include <lib/fidl/cpp/clone.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fit/function.h>

#include <memory>
#include <string>

#include "src/ledger/bin/app/merging/auto_merge_strategy.h"
#include "src/ledger/bin/app/merging/custom_merge_strategy.h"
#include "src/ledger/bin/app/merging/last_one_wins_merge_strategy.h"
#include "src/ledger/bin/app/merging/merge_resolver.h"
#include "src/ledger/lib/backoff/exponential_backoff.h"

namespace ledger {

class LedgerMergeManager::ConflictResolverFactoryPtrContainer {
 public:
  explicit ConflictResolverFactoryPtrContainer(
      fidl::InterfaceHandle<ConflictResolverFactory> factory)
      : ptr_(factory.Bind()) {
    ptr_.set_error_handler([this](zx_status_t status) { OnDiscardable(); });
  }

  void SetOnDiscardable(fit::closure on_discardable) {
    on_discardable_ = std::move(on_discardable);
  }

  bool IsDiscardable() const { return !ptr_.is_bound(); }

  // Returns the pointer and disappear from the AutoCleanableMap
  ConflictResolverFactoryPtr TakePtr() {
    ConflictResolverFactoryPtr ptr = std::move(ptr_);
    ptr.set_error_handler(nullptr);
    // Deletes |this|
    OnDiscardable();
    return ptr;
  }

 private:
  // Deletes the object when in an AutoCleanableMap
  void OnDiscardable() {
    if (on_discardable_)
      on_discardable_();
  }

  ConflictResolverFactoryPtr ptr_;
  fit::closure on_discardable_;
};

LedgerMergeManager::LedgerMergeManager(Environment* environment)
    : environment_(environment), conflict_resolver_factories_(environment->dispatcher()) {}

LedgerMergeManager::~LedgerMergeManager() = default;

void LedgerMergeManager::AddFactory(fidl::InterfaceHandle<ConflictResolverFactory> factory) {
  using_default_conflict_resolver_ = false;

  conflict_resolver_factories_.emplace(std::move(factory));

  if (!current_conflict_resolver_factory_) {
    ResetFactory();
  }
}

void LedgerMergeManager::ResetFactory() {
  if (conflict_resolver_factories_.empty())
    return;

  current_conflict_resolver_factory_ = conflict_resolver_factories_.begin()->TakePtr();
  current_conflict_resolver_factory_.set_error_handler(
      [this](zx_status_t status) { this->ResetFactory(); });

  for (const auto& item : resolvers_) {
    item.second->SetMergeStrategy(nullptr);
    GetResolverStrategyForPage(
        item.first, [this, page_id = item.first](std::unique_ptr<MergeStrategy> strategy) mutable {
          if (resolvers_.find(page_id) != resolvers_.end()) {
            resolvers_[page_id]->SetMergeStrategy(std::move(strategy));
          }
        });
  }
}

void LedgerMergeManager::RemoveResolver(const storage::PageId& page_id) {
  resolvers_.erase(page_id);
}

std::unique_ptr<MergeResolver> LedgerMergeManager::GetMergeResolver(storage::PageStorage* storage) {
  storage::PageId page_id = storage->GetId();
  std::unique_ptr<MergeResolver> resolver = std::make_unique<MergeResolver>(
      [this, page_id]() { RemoveResolver(page_id); }, environment_, storage,
      std::make_unique<ExponentialBackoff>(zx::msec(10), 2u, zx::sec(60 * 60),
                                           environment_->random()->NewBitGenerator<uint64_t>()));
  resolvers_[page_id] = resolver.get();
  GetResolverStrategyForPage(page_id,
                             [this, page_id](std::unique_ptr<MergeStrategy> strategy) mutable {
                               if (resolvers_.find(page_id) != resolvers_.end()) {
                                 resolvers_[page_id]->SetMergeStrategy(std::move(strategy));
                               }
                             });
  return resolver;
}

void LedgerMergeManager::GetResolverStrategyForPage(
    const storage::PageId& page_id,
    fit::function<void(std::unique_ptr<MergeStrategy>)> strategy_callback) {
  if (using_default_conflict_resolver_) {
    strategy_callback(std::make_unique<LastOneWinsMergeStrategy>());
  } else if (!current_conflict_resolver_factory_) {
    // When no |ConflictResolverFactory| is connected, no conflict resolution
    // happens for pages where conflict resolution has not been setup.
    // Conflict resolution continues for pages that already have a policy.
  } else {
    PageId converted_page_id;
    convert::ToArray(page_id, &converted_page_id.id);
    current_conflict_resolver_factory_->GetPolicy(
        converted_page_id, [this, page_id, converted_page_id,
                            strategy_callback = std::move(strategy_callback)](MergePolicy policy) {
          switch (policy) {
            case MergePolicy::LAST_ONE_WINS:
              strategy_callback(std::make_unique<LastOneWinsMergeStrategy>());
              break;
            case MergePolicy::AUTOMATIC_WITH_FALLBACK: {
              ConflictResolverPtr conflict_resolver;
              current_conflict_resolver_factory_->NewConflictResolver(
                  converted_page_id, conflict_resolver.NewRequest());
              std::unique_ptr<AutoMergeStrategy> auto_merge_strategy =
                  std::make_unique<AutoMergeStrategy>(std::move(conflict_resolver));
              auto_merge_strategy->SetOnError([this, page_id]() { ResetStrategyForPage(page_id); });
              strategy_callback(std::move(auto_merge_strategy));
              break;
            }
            case MergePolicy::CUSTOM: {
              PageId converted_page_id;
              convert::ToArray(page_id, &converted_page_id.id);
              ConflictResolverPtr conflict_resolver;
              current_conflict_resolver_factory_->NewConflictResolver(
                  converted_page_id, conflict_resolver.NewRequest());
              std::unique_ptr<CustomMergeStrategy> custom_merge_strategy =
                  std::make_unique<CustomMergeStrategy>(std::move(conflict_resolver));
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
  GetResolverStrategyForPage(page_id,
                             [this, page_id](std::unique_ptr<MergeStrategy> strategy) mutable {
                               if (resolvers_.find(page_id) != resolvers_.end()) {
                                 resolvers_[page_id]->SetMergeStrategy(std::move(strategy));
                               }
                             });
}
}  // namespace ledger
