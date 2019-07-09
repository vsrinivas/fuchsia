// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/page_manager.h"

#include <lib/async/cpp/task.h>
#include <lib/callback/ensure_called.h>
#include <lib/callback/scoped_callback.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/inspect_deprecated/inspect.h>
#include <trace/event.h>

#include <string>
#include <utility>
#include <vector>

#include "src/ledger/bin/app/active_page_manager_container.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/ledger_impl.h"
#include "src/ledger/bin/app/page_connection_notifier.h"
#include "src/ledger/bin/app/page_manager.h"
#include "src/ledger/bin/app/page_utils.h"
#include "src/ledger/bin/app/types.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/inspect/inspect.h"
#include "src/ledger/bin/p2p_sync/public/page_communicator.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace ledger {

PageManager::PageManager(Environment* environment, std::string ledger_name, storage::PageId page_id,
                         PageUsageListener* page_usage_listener,
                         storage::LedgerStorage* ledger_storage,
                         sync_coordinator::LedgerSync* ledger_sync,
                         LedgerMergeManager* ledger_merge_manager,
                         inspect_deprecated::Node inspect_node)
    : environment_(environment),
      ledger_name_(std::move(ledger_name)),
      page_id_(std::move(page_id)),
      page_usage_listener_(page_usage_listener),
      ledger_storage_(ledger_storage),
      ledger_sync_(ledger_sync),
      ledger_merge_manager_(ledger_merge_manager),
      inspect_node_(std::move(inspect_node)),
      commits_node_(inspect_node_.CreateChild(kCommitsInspectPathComponent.ToString())),
      weak_factory_(this) {
  page_availability_manager_.set_on_empty([this] { CheckEmpty(); });
}

PageManager::~PageManager() {}

fit::closure PageManager::CreateDetacher() {
  outstanding_detachers_++;
  return [this]() {
    outstanding_detachers_--;
    FXL_DCHECK(outstanding_detachers_ >= 0);
    CheckEmpty();
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
  page_availability_manager_.MarkPageBusy(page_id_);
  ledger_storage_->DeletePageStorage(
      page_id_,
      callback::MakeScoped(weak_factory_.GetWeakPtr(),
                           [this, callback = std::move(callback)](storage::Status status) {
                             // This may destruct this
                             // |PageManager|.
                             page_availability_manager_.MarkPageAvailable(page_id_);
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

void PageManager::InitActivePageManagerContainer(ActivePageManagerContainer* container,
                                                 fit::function<void(storage::Status)> callback) {
  page_availability_manager_.OnPageAvailable(
      page_id_, [this, container, callback = std::move(callback)]() mutable {
        ledger_storage_->GetPageStorage(
            page_id_, [this, container, callback = std::move(callback)](
                          storage::Status status,
                          std::unique_ptr<storage::PageStorage> page_storage) mutable {
              if (status != storage::Status::OK && status != storage::Status::PAGE_NOT_FOUND) {
                container->SetActivePageManager(status, nullptr);
                callback(status);
                return;
              }

              // If the page was found locally, just use it and return.
              if (status == storage::Status::OK) {
                FXL_DCHECK(page_storage);
                container->SetActivePageManager(
                    storage::Status::OK,
                    NewActivePageManager(std::move(page_storage),
                                         ActivePageManager::PageStorageState::AVAILABLE));
              }
              callback(status);
            });
      });
}

void PageManager::CreatePageStorage(LedgerImpl::Delegate::PageState page_state,
                                    ActivePageManagerContainer* container) {
  page_availability_manager_.OnPageAvailable(page_id_, [this, page_state, container]() mutable {
    ledger_storage_->CreatePageStorage(
        page_id_, [this, page_state, container](
                      storage::Status status, std::unique_ptr<storage::PageStorage> page_storage) {
          if (status != storage::Status::OK) {
            container->SetActivePageManager(status, nullptr);
            return;
          }
          container->SetActivePageManager(
              storage::Status::OK,
              NewActivePageManager(std::move(page_storage),
                                   page_state == LedgerImpl::Delegate::PageState::NEW
                                       ? ActivePageManager::PageStorageState::AVAILABLE
                                       : ActivePageManager::PageStorageState::NEEDS_SYNC));
        });
  });
}

ActivePageManagerContainer* PageManager::CreateActivePageManagerContainer() {
  FXL_DCHECK(!active_page_manager_container_);
  auto& active_page_manager_container =
      active_page_manager_container_.emplace(ledger_name_, page_id_, page_usage_listener_);
  active_page_manager_container_->set_on_empty([this]() {
    active_page_manager_container_.reset();
    CheckEmpty();
  });
  return &active_page_manager_container;
}

std::unique_ptr<ActivePageManager> PageManager::NewActivePageManager(
    std::unique_ptr<storage::PageStorage> page_storage, ActivePageManager::PageStorageState state) {
  std::unique_ptr<sync_coordinator::PageSync> page_sync;
  if (ledger_sync_) {
    page_sync = ledger_sync_->CreatePageSync(page_storage.get(), page_storage.get());
  }
  return std::make_unique<ActivePageManager>(
      environment_, std::move(page_storage), std::move(page_sync),
      ledger_merge_manager_->GetMergeResolver(page_storage.get()), state);
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
    FXL_DCHECK(active_page_manager);
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
    auto check_empty_on_return = fit::defer([this] { CheckEmpty(); });

    if (was_opened_.empty()) {
      return false;
    }
    // TODO(https://fuchsia.atlassian.net/browse/LE-769): Drop this if; it's not
    // correct and if it were it would be redundant with the
    // immediately-following statements.
    if (was_opened_.size() == 1) {
      // This is the last outstanding operation; clear the vector.
      was_opened_.clear();
      return true;
    }
    // Erase the operation_id, if found, from the found vector (it->second).
    auto operation_it = std::find(was_opened_.begin(), was_opened_.end(), operation_id);
    if (operation_it != was_opened_.end()) {
      was_opened_.erase(operation_it);
      return true;
    }
    return false;
  };
  return callback::EnsureCalled(std::move(stop_tracking));
}

void PageManager::MaybeMarkPageOpened() { was_opened_.clear(); }

void PageManager::CheckEmpty() {
  if (on_empty_callback_ && !active_page_manager_container_ && outstanding_operations_ == 0 &&
      page_availability_manager_.IsEmpty() && outstanding_detachers_ == 0) {
    on_empty_callback_();
  }
}

}  // namespace ledger
