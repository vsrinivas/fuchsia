// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/ledger_manager.h"

#include <string>
#include <utility>
#include <vector>

#include <lib/callback/ensure_called.h>
#include <lib/callback/scoped_callback.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/memory/weak_ptr.h>
#include <trace/event.h>

#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/page_connection_notifier.h"
#include "src/ledger/bin/app/page_manager_container.h"
#include "src/ledger/bin/app/page_utils.h"
#include "src/ledger/bin/app/types.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/p2p_sync/public/page_communicator.h"
#include "src/ledger/bin/storage/public/page_storage.h"

namespace ledger {

LedgerManager::LedgerManager(
    Environment* environment, std::string ledger_name,
    std::unique_ptr<encryption::EncryptionService> encryption_service,
    std::unique_ptr<storage::LedgerStorage> storage,
    std::unique_ptr<sync_coordinator::LedgerSync> ledger_sync,
    PageUsageListener* page_usage_listener)
    : environment_(environment),
      ledger_name_(std::move(ledger_name)),
      encryption_service_(std::move(encryption_service)),
      storage_(std::move(storage)),
      ledger_sync_(std::move(ledger_sync)),
      ledger_impl_(environment_, this),
      merge_manager_(environment_),
      page_usage_listener_(page_usage_listener),
      weak_factory_(this) {
  bindings_.set_on_empty([this] { CheckEmpty(); });
  page_managers_.set_on_empty([this] { CheckEmpty(); });
  page_availability_manager_.set_on_empty([this] { CheckEmpty(); });
}

LedgerManager::~LedgerManager() {}

void LedgerManager::BindLedger(fidl::InterfaceRequest<Ledger> ledger_request) {
  bindings_.emplace(&ledger_impl_, std::move(ledger_request));
}

void LedgerManager::PageIsClosedAndSynced(
    storage::PageIdView page_id,
    fit::function<void(storage::Status, PagePredicateResult)> callback) {
  auto is_synced = [](PageManager* page_manager,
                      fit::function<void(storage::Status, bool)> on_done) {
    page_manager->IsSynced(std::move(on_done));
  };
  PageIsClosedAndSatisfiesPredicate(page_id, std::move(is_synced),
                                    std::move(callback));
}

void LedgerManager::PageIsClosedOfflineAndEmpty(
    storage::PageIdView page_id,
    fit::function<void(storage::Status, PagePredicateResult)> callback) {
  auto is_offline_and_empty =
      [](PageManager* page_manager,
         fit::function<void(storage::Status, bool)> on_done) {
        page_manager->IsOfflineAndEmpty(std::move(on_done));
      };
  PageIsClosedAndSatisfiesPredicate(page_id, std::move(is_offline_and_empty),
                                    std::move(callback));
}

void LedgerManager::DeletePageStorage(
    convert::ExtendedStringView page_id,
    fit::function<void(storage::Status)> callback) {
  auto it = page_managers_.find(page_id);
  if (it != page_managers_.end()) {
    callback(storage::Status::ILLEGAL_STATE);
    return;
  }

  // Block all page requests until deletion is complete.
  page_availability_manager_.MarkPageBusy(page_id);
  storage_->DeletePageStorage(
      page_id, callback::MakeScoped(
                   weak_factory_.GetWeakPtr(),
                   [this, page_id = page_id.ToString(),
                    callback = std::move(callback)](storage::Status status) {
                     // This may destruct the |LedgerManager|.
                     page_availability_manager_.MarkPageAvailable(page_id);
                     callback(status);
                   }));
}

void LedgerManager::GetPage(storage::PageIdView page_id, PageState page_state,
                            fidl::InterfaceRequest<Page> page_request,
                            fit::function<void(storage::Status)> callback) {
  MaybeMarkPageOpened(page_id);

  // If we have the page manager ready, just bind the request and return.
  auto it = page_managers_.find(page_id);
  if (it != page_managers_.end()) {
    it->second.BindPage(std::move(page_request), std::move(callback));
    return;
  }

  PageManagerContainer* container = AddPageManagerContainer(page_id);
  // TODO(LE-631): We will need to remove empty pages that are unknown to the
  // user or the page usage database.
  container->BindPage(std::move(page_request), std::move(callback));

  InitPageManagerContainer(container, page_id,
                           [this, container, page_id = page_id.ToString(),
                            page_state](storage::Status status) mutable {
                             // Create the page if it wasn't found.
                             if (status == storage::Status::PAGE_NOT_FOUND) {
                               CreatePageStorage(std::move(page_id), page_state,
                                                 container);
                             }
                           });
}

void LedgerManager::InitPageManagerContainer(
    PageManagerContainer* container, convert::ExtendedStringView page_id,
    fit::function<void(storage::Status)> callback) {
  page_availability_manager_.OnPageAvailable(
      page_id, [this, container, page_id = page_id.ToString(),
                callback = std::move(callback)]() mutable {
        storage_->GetPageStorage(
            std::move(page_id),
            [this, container, callback = std::move(callback)](
                storage::Status status,
                std::unique_ptr<storage::PageStorage> page_storage) mutable {
              if (status != storage::Status::OK &&
                  status != storage::Status::PAGE_NOT_FOUND) {
                container->SetPageManager(status, nullptr);
                callback(status);
                return;
              }

              // If the page was found locally, just use it and return.
              if (status == storage::Status::OK) {
                FXL_DCHECK(page_storage);
                container->SetPageManager(
                    storage::Status::OK,
                    NewPageManager(std::move(page_storage),
                                   PageManager::PageStorageState::AVAILABLE));
              }
              callback(status);
            });
      });
}

void LedgerManager::CreatePageStorage(storage::PageId page_id,
                                      PageState page_state,
                                      PageManagerContainer* container) {
  page_availability_manager_.OnPageAvailable(
      page_id,
      [this, page_id = std::move(page_id), page_state, container]() mutable {
        storage_->CreatePageStorage(
            std::move(page_id),
            [this, page_state, container](
                storage::Status status,
                std::unique_ptr<storage::PageStorage> page_storage) {
              if (status != storage::Status::OK) {
                container->SetPageManager(status, nullptr);
                return;
              }
              container->SetPageManager(
                  storage::Status::OK,
                  NewPageManager(
                      std::move(page_storage),
                      page_state == PageState::NEW
                          ? PageManager::PageStorageState::AVAILABLE
                          : PageManager::PageStorageState::NEEDS_SYNC));
            });
      });
}

PageManagerContainer* LedgerManager::AddPageManagerContainer(
    storage::PageIdView page_id) {
  auto ret = page_managers_.emplace(
      std::piecewise_construct, std::forward_as_tuple(page_id.ToString()),
      std::forward_as_tuple(ledger_name_, page_id.ToString(),
                            page_usage_listener_));
  FXL_DCHECK(ret.second);
  return &ret.first->second;
}

std::unique_ptr<PageManager> LedgerManager::NewPageManager(
    std::unique_ptr<storage::PageStorage> page_storage,
    PageManager::PageStorageState state) {
  std::unique_ptr<sync_coordinator::PageSync> page_sync;
  if (ledger_sync_) {
    page_sync =
        ledger_sync_->CreatePageSync(page_storage.get(), page_storage.get());
  }
  return std::make_unique<PageManager>(
      environment_, std::move(page_storage), std::move(page_sync),
      merge_manager_.GetMergeResolver(page_storage.get()), state);
}

void LedgerManager::PageIsClosedAndSatisfiesPredicate(
    storage::PageIdView page_id,
    fit::function<void(PageManager*,
                       fit::function<void(storage::Status, bool)>)>
        predicate,
    fit::function<void(storage::Status, PagePredicateResult)> callback_unsafe) {
  // Ensure that the callback will be called, whatever happens.
  auto callback = callback::EnsureCalled(std::move(callback_unsafe),
                                         storage::Status::ILLEGAL_STATE,
                                         PagePredicateResult::PAGE_OPENED);

  // Start logging whether the page has been opened during the execution of
  // this method.
  auto tracker = NewPageTracker(page_id);

  PageManagerContainer* container;

  auto it = page_managers_.find(page_id);
  if (it != page_managers_.end()) {
    // The page manager is open, check if there are any open connections.
    container = &it->second;
    if (container->PageConnectionIsOpen()) {
      callback(storage::Status::OK, PagePredicateResult::PAGE_OPENED);
      return;
    }
  } else {
    // Create a new container and get the PageStorage.
    container = AddPageManagerContainer(page_id);
    InitPageManagerContainer(container, page_id,
                             [container](storage::Status status) {
                               if (status == storage::Status::PAGE_NOT_FOUND) {
                                 container->SetPageManager(status, nullptr);
                               }
                             });
  }

  container->NewInternalRequest([this, page_id = page_id.ToString(),
                                 tracker = std::move(tracker),
                                 predicate = std::move(predicate),
                                 callback = std::move(callback)](
                                    storage::Status status, ExpiringToken token,
                                    PageManager* page_manager) mutable {
    if (status != storage::Status::OK) {
      callback(status, PagePredicateResult::PAGE_OPENED);
      return;
    }
    FXL_DCHECK(page_manager);
    // The page_manager may be destructed before we complete.
    auto weak_this = weak_factory_.GetWeakPtr();
    predicate(page_manager,
              [this, page_id = std::move(page_id), tracker = std::move(tracker),
               callback = std::move(callback), token = std::move(token),
               weak_this](storage::Status status, bool condition) mutable {
                if (status != storage::Status::OK) {
                  callback(status, PagePredicateResult::PAGE_OPENED);
                }
                if (!weak_this) {
                  // |callback| is called on destruction.
                  return;
                }
                // |token| is expected to go out of scope.
                async::PostTask(environment_->dispatcher(),
                                [condition, page_id = std::move(page_id),
                                 callback = std::move(callback),
                                 tracker = std::move(tracker)]() mutable {
                                  if (!tracker()) {
                                    // If |RemoveTrackedPage| returns false,
                                    // this means that the page was opened
                                    // during this operation and |PAGE_OPENED|
                                    // must be returned.
                                    callback(storage::Status::OK,
                                             PagePredicateResult::PAGE_OPENED);
                                    return;
                                  }
                                  callback(storage::Status::OK,
                                           condition ? PagePredicateResult::YES
                                                     : PagePredicateResult::NO);
                                });
              });
  });
}

fit::function<bool()> LedgerManager::NewPageTracker(
    storage::PageIdView page_id) {
  tracked_pages_++;
  uint64_t operation_id = page_was_opened_id_++;
  page_was_opened_map_[page_id.ToString()].push_back(operation_id);

  fxl::WeakPtr<LedgerManager> weak_this = weak_factory_.GetWeakPtr();

  auto stop_tracking = [this, weak_this, page_id = page_id.ToString(),
                        operation_id] {
    if (!weak_this) {
      return false;
    }
    tracked_pages_--;
    auto it = page_was_opened_map_.find(page_id);
    if (it == page_was_opened_map_.end()) {
      return false;
    }
    if (it->second.size() == 1) {
      // This is the last operation for this page: delete the page's entry.
      page_was_opened_map_.erase(it);
      return true;
    }
    // Erase the operation_id, if found, from the found vector (it->second).
    auto operation_it =
        std::find(it->second.begin(), it->second.end(), operation_id);
    if (operation_it != it->second.end()) {
      it->second.erase(operation_it);
      return true;
    }
    return false;
  };
  return callback::EnsureCalled(std::move(stop_tracking));
}

void LedgerManager::MaybeMarkPageOpened(storage::PageIdView page_id) {
  page_was_opened_map_.erase(page_id.ToString());
}

void LedgerManager::CheckEmpty() {
  if (on_empty_callback_ && bindings_.size() == 0 && page_managers_.empty() &&
      tracked_pages_ == 0 && page_availability_manager_.IsEmpty()) {
    on_empty_callback_();
  }
}

void LedgerManager::SetConflictResolverFactory(
    fidl::InterfaceHandle<ConflictResolverFactory> factory) {
  merge_manager_.AddFactory(std::move(factory));
}

}  // namespace ledger
