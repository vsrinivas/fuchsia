// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/page_manager.h"

#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>

#include <string>
#include <utility>
#include <vector>

#include <trace/event.h>

#include "src/ledger/bin/app/active_page_manager.h"
#include "src/ledger/bin/app/active_page_manager_container.h"
#include "src/ledger/bin/app/commits_children_manager.h"
#include "src/ledger/bin/app/heads_children_manager.h"
#include "src/ledger/bin/app/ledger_impl.h"
#include "src/ledger/bin/app/page_utils.h"
#include "src/ledger/bin/app/types.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/inspect/inspect.h"
#include "src/ledger/bin/p2p_sync/public/page_communicator.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/callback/ensure_called.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/lib/callback/scoped_callback.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/lib/inspect_deprecated/inspect.h"

namespace ledger {

PageManager::PageManager(Environment* environment, std::string ledger_name, storage::PageId page_id,
                         std::vector<PageUsageListener*> page_usage_listeners,
                         storage::LedgerStorage* ledger_storage,
                         sync_coordinator::LedgerSync* ledger_sync,
                         LedgerMergeManager* ledger_merge_manager,
                         inspect_deprecated::Node inspect_node)
    : environment_(environment),
      ledger_name_(std::move(ledger_name)),
      page_id_(std::move(page_id)),
      page_usage_listeners_(std::move(page_usage_listeners)),
      ledger_storage_(ledger_storage),
      ledger_sync_(ledger_sync),
      ledger_merge_manager_(ledger_merge_manager),
      inspect_node_(std::move(inspect_node)),
      heads_node_(inspect_node_.CreateChild(convert::ToString(kHeadsInspectPathComponent))),
      heads_children_manager_(environment_->dispatcher(), &heads_node_, this),
      heads_children_manager_retainer_(heads_node_.SetChildrenManager(&heads_children_manager_)),
      commits_node_(inspect_node_.CreateChild(convert::ToString(kCommitsInspectPathComponent))),
      commits_children_manager_(environment_->dispatcher(), &commits_node_, this),
      commits_children_manager_retainer_(
          commits_node_.SetChildrenManager(&commits_children_manager_)),
      weak_factory_(this) {
  page_availability_manager_.SetOnDiscardable([this] { CheckDiscardable(); });
  heads_children_manager_.SetOnDiscardable([this] { CheckDiscardable(); });
  commits_children_manager_.SetOnDiscardable([this] { CheckDiscardable(); });
}

PageManager::~PageManager() = default;

fit::closure PageManager::CreateDetacher() {
  outstanding_detachers_++;
  return [this]() {
    outstanding_detachers_--;
    LEDGER_DCHECK(outstanding_detachers_ >= 0);
    CheckDiscardable();
  };
}

void PageManager::PageIsClosedAndSynced(
    fit::function<void(storage::Status, PagePredicateResult)> callback) {
  auto is_synced = [](ActivePageManager* active_page_manager,
                      fit::function<void(storage::Status, bool)> on_done) {
    active_page_manager->IsSynced(std::move(on_done));
  };
  PageIsClosedAndSatisfiesPredicate(std::move(is_synced), std::move(callback));
}

void PageManager::PageIsClosedOfflineAndEmpty(
    fit::function<void(storage::Status, PagePredicateResult)> callback) {
  auto is_offline_and_empty = [](ActivePageManager* active_page_manager,
                                 fit::function<void(storage::Status, bool)> on_done) {
    active_page_manager->IsOfflineAndEmpty(std::move(on_done));
  };
  PageIsClosedAndSatisfiesPredicate(std::move(is_offline_and_empty), std::move(callback));
}

void PageManager::DeletePageStorage(fit::function<void(storage::Status)> callback) {
  if (active_page_manager_container_) {
    callback(storage::Status::ILLEGAL_STATE);
    return;
  }
  // Block all page requests until MarkAvailable is called.
  page_availability_manager_.MarkPageBusy();
  ledger_storage_->DeletePageStorage(
      page_id_,
      callback::MakeScoped(weak_factory_.GetWeakPtr(),
                           [this, callback = std::move(callback)](storage::Status status) {
                             // This may destruct this
                             // |PageManager|.
                             page_availability_manager_.MarkPageAvailable();
                             callback(status);
                           }));
}

void PageManager::GetPage(LedgerImpl::Delegate::PageState page_state,
                          fidl::InterfaceRequest<Page> page_request,
                          fit::function<void(storage::Status)> callback) {
  MaybeMarkPageOpened();

  // If we have the page manager ready, just bind the request and return.
  if (active_page_manager_container_) {
    active_page_manager_container_.value().BindPage(std::move(page_request), std::move(callback));
    return;
  }

  ActivePageManagerContainer* container = CreateActivePageManagerContainer();
  // TODO(LE-631): We will need to remove empty pages that are unknown to the
  // user or the page usage database.
  container->BindPage(std::move(page_request), std::move(callback));

  InitActivePageManagerContainer(container,
                                 [this, container, page_state](storage::Status status) mutable {
                                   // Create the page if it wasn't found.
                                   if (status == storage::Status::PAGE_NOT_FOUND) {
                                     CreatePageStorage(page_state, container);
                                   }
                                 });
}

void PageManager::NewInspection(fit::function<void(storage::Status status, ExpiringToken token,
                                                   ActivePageManager* active_page_manager)>
                                    callback) {
  if (!active_page_manager_container_) {
    ActivePageManagerContainer* container = CreateActivePageManagerContainer();
    InitActivePageManagerContainer(container, [](Status status) {
      if (status != Status::OK) {
        // It's odd that the page storage failed to come up; we will just report to Inspect that
        // there is no data for the page.
        LEDGER_LOG(WARNING) << "Tried to bring up a page for Inspect but got status: " << status;
      }
    });
  }
  active_page_manager_container_->NewInternalRequest(std::move(callback));
}

void PageManager::SetOnDiscardable(fit::closure on_discardable) {
  on_discardable_ = std::move(on_discardable);
}

bool PageManager::IsDiscardable() const {
  return (!active_page_manager_container_ || active_page_manager_container_->IsDiscardable()) &&
         outstanding_operations_ == 0 && page_availability_manager_.IsDiscardable() &&
         outstanding_detachers_ == 0 && heads_children_manager_.IsDiscardable() &&
         commits_children_manager_.IsDiscardable();
}

void PageManager::StartPageSync() {
  // If the active page manager container is open, just return as the sync should have been already
  // triggered.
  if (active_page_manager_container_) {
    return;
  }

  // Create the container and set up an active page manager to start sync with the cloud.
  ActivePageManagerContainer* container = CreateActivePageManagerContainer();
  InitActivePageManagerContainer(container, [container](storage::Status status) {
    // InitActivePageManager does not handle the case of PAGE_NOT_FOUND errors to allow creation of
    // a new page, but no creation is required here, so status is merely propagated.
    if (status == storage::Status::PAGE_NOT_FOUND) {
      container->SetActivePageManager(status, nullptr);
    }
  });
}

void PageManager::InitActivePageManagerContainer(ActivePageManagerContainer* container,
                                                 fit::function<void(storage::Status)> callback) {
  page_availability_manager_.OnPageAvailable([this, container,
                                              callback = std::move(callback)]() mutable {
    ledger_storage_->GetPageStorage(
        page_id_,
        [this, container, callback = std::move(callback)](
            storage::Status status, std::unique_ptr<storage::PageStorage> page_storage) mutable {
          if (status != storage::Status::OK && status != storage::Status::PAGE_NOT_FOUND) {
            container->SetActivePageManager(status, nullptr);
            callback(status);
            return;
          }
          if (status == storage::Status::PAGE_NOT_FOUND) {
            callback(status);
            return;
          }

          // If the page was found locally, just use it and return.
          LEDGER_DCHECK(page_storage);
          NewActivePageManager(
              std::move(page_storage), ActivePageManager::PageStorageState::AVAILABLE,
              [container, callback = std::move(callback)](
                  storage::Status status, std::unique_ptr<ActivePageManager> active_page_manager) {
                container->SetActivePageManager(status, std::move(active_page_manager));
                callback(status);
              });
        });
  });
}

void PageManager::CreatePageStorage(LedgerImpl::Delegate::PageState page_state,
                                    ActivePageManagerContainer* container) {
  page_availability_manager_.OnPageAvailable([this, page_state, container]() mutable {
    ledger_storage_->CreatePageStorage(
        page_id_, [this, page_state, container](
                      storage::Status status, std::unique_ptr<storage::PageStorage> page_storage) {
          if (status != storage::Status::OK) {
            container->SetActivePageManager(status, nullptr);
            return;
          }

          NewActivePageManager(std::move(page_storage),
                               page_state == LedgerImpl::Delegate::PageState::NEW
                                   ? ActivePageManager::PageStorageState::AVAILABLE
                                   : ActivePageManager::PageStorageState::NEEDS_SYNC,
                               [container](storage::Status status,
                                           std::unique_ptr<ActivePageManager> active_page_manager) {
                                 container->SetActivePageManager(status,
                                                                 std::move(active_page_manager));
                               });
        });
  });
}

ActivePageManagerContainer* PageManager::CreateActivePageManagerContainer() {
  LEDGER_DCHECK(!active_page_manager_container_);
  auto& active_page_manager_container = active_page_manager_container_.emplace(
      environment_, ledger_name_, page_id_, page_usage_listeners_);
  active_page_manager_container_->SetOnDiscardable([this]() {
    active_page_manager_container_.reset();
    CheckDiscardable();
  });
  return &active_page_manager_container;
}

void PageManager::NewActivePageManager(
    std::unique_ptr<storage::PageStorage> page_storage, ActivePageManager::PageStorageState state,
    fit::function<void(storage::Status, std::unique_ptr<ActivePageManager>)> callback) {
  if (!ledger_sync_) {
    auto result = std::make_unique<ActivePageManager>(
        environment_, std::move(page_storage), nullptr,
        ledger_merge_manager_->GetMergeResolver(page_storage.get()), state);
    callback(storage::Status::OK, std::move(result));
    return;
  }

  ledger_sync_->CreatePageSync(
      page_storage.get(), page_storage.get(),
      [this, page_storage = std::move(page_storage), state, callback = std::move(callback)](
          storage::Status status, std::unique_ptr<sync_coordinator::PageSync> page_sync) mutable {
        if (status != storage::Status::OK) {
          callback(status, nullptr);
          return;
        }
        auto result = std::make_unique<ActivePageManager>(
            environment_, std::move(page_storage), std::move(page_sync),
            ledger_merge_manager_->GetMergeResolver(page_storage.get()), state);
        callback(storage::Status::OK, std::move(result));
      });
}

void PageManager::PageIsClosedAndSatisfiesPredicate(
    fit::function<void(ActivePageManager*, fit::function<void(storage::Status, bool)>)> predicate,
    fit::function<void(storage::Status, PagePredicateResult)> callback) {
  // Start logging whether the page has been opened during the execution of
  // this method.
  auto tracker = NewPageTracker();

  ActivePageManagerContainer* container;

  if (active_page_manager_container_) {
    // The page manager is open, check if there are any open connections.
    container = &active_page_manager_container_.value();
    if (active_page_manager_container_->PageConnectionIsOpen()) {
      callback(storage::Status::OK, PagePredicateResult::PAGE_OPENED);
      return;
    }
  } else {
    // Create the container and get the PageStorage.
    container = CreateActivePageManagerContainer();
    InitActivePageManagerContainer(container, [container](storage::Status status) {
      if (status == storage::Status::PAGE_NOT_FOUND) {
        container->SetActivePageManager(status, nullptr);
      }
    });
  }

  container->NewInternalRequest([this, tracker = std::move(tracker),
                                 predicate = std::move(predicate), callback = std::move(callback)](
                                    storage::Status status, ExpiringToken token,
                                    ActivePageManager* active_page_manager) mutable {
    if (status != storage::Status::OK) {
      callback(status, PagePredicateResult::PAGE_OPENED);
      return;
    }
    LEDGER_DCHECK(active_page_manager);
    predicate(active_page_manager,
              callback::MakeScoped(
                  weak_factory_.GetWeakPtr(),
                  [this, tracker = std::move(tracker), callback = std::move(callback),
                   token = std::move(token)](storage::Status status, bool condition) mutable {
                    if (status != storage::Status::OK) {
                      callback(status, PagePredicateResult::PAGE_OPENED);
                    }
                    // |token| is expected to go out of scope. The
                    // PageManager is kept non-empty by |tracker|.
                    async::PostTask(
                        environment_->dispatcher(),
                        callback::MakeScoped(
                            weak_factory_.GetWeakPtr(), [condition, callback = std::move(callback),
                                                         tracker = std::move(tracker)]() mutable {
                              if (!tracker()) {
                                // If |RemoveTrackedPage| returns false, this
                                // means that the page was opened during this
                                // operation and |PAGE_OPENED| must be returned.
                                callback(storage::Status::OK, PagePredicateResult::PAGE_OPENED);
                                return;
                              }
                              callback(storage::Status::OK, condition ? PagePredicateResult::YES
                                                                      : PagePredicateResult::NO);
                            }));
                  }));
  });
}

fit::function<bool()> PageManager::NewPageTracker() {
  outstanding_operations_++;
  uint64_t operation_id = was_opened_id_++;
  was_opened_.push_back(operation_id);

  fxl::WeakPtr<PageManager> weak_this = weak_factory_.GetWeakPtr();

  auto stop_tracking = [this, weak_this, operation_id]() {
    if (!weak_this) {
      return false;
    }
    outstanding_operations_--;
    auto check_empty_on_return = fit::defer([this] { CheckDiscardable(); });

    // Erase operation_id, if found, from the vector - operation_id may not be present in the vector
    // if the vector was cleared (as happens during a call to GetPage) between when the operation
    // started and now.
    auto it = std::find(was_opened_.begin(), was_opened_.end(), operation_id);
    if (it != was_opened_.end()) {
      was_opened_.erase(it);
      return true;
    }
    return false;
  };
  return EnsureCalled(std::move(stop_tracking));
}

void PageManager::MaybeMarkPageOpened() { was_opened_.clear(); }

void PageManager::CheckDiscardable() {
  if (on_discardable_ && IsDiscardable()) {
    on_discardable_();
  }
}

}  // namespace ledger
