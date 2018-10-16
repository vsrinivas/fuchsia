// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/ledger_manager.h"

#include <string>
#include <utility>
#include <vector>

#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/memory/weak_ptr.h>
#include <trace/event.h>

#include "peridot/bin/ledger/app/constants.h"
#include "peridot/bin/ledger/app/page_utils.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/p2p_sync/public/page_communicator.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"

namespace ledger {
namespace {

// A token that performs a given action on destruction.
// ExpiringToken objects are used with internal page requests to notify the
// PageManagerContainer that the requested PageManager is no longer used.
using ExpiringToken = fit::deferred_action<fit::closure>;

// A notifier for |PageUsageListener|.
//
// Given information about when internal and external page connections open and
// close, |PageConnectionNotifier| calls the corresponding methods from
// |PageUsageListener|. The |PageUsageListener| given in the constructor should
// outlive this object.
class PageConnectionNotifier {
 public:
  PageConnectionNotifier(std::string ledger_name, storage::PageId page_id,
                         PageUsageListener* page_usage_listener);
  ~PageConnectionNotifier();

  // Registers a new external page request.
  void RegisterExternalRequest();

  // Unregisters all active external page requests. This can be because all
  // active connections were closed, or because of failure to bind the requests.
  void UnregisterExternalRequests();

  // Registers a new internal page request.
  void RegisterInternalRequest();

  // Unregisters one active internal page request. This can be because the
  // active connection was closed, or because of failure to fulfill the request.
  void UnregisterInternalRequest();

  // Sets the on_empty callaback, to be called every time this object becomes
  // empty.
  void set_on_empty(fit::closure on_empty_callback);

  // Checks and returns whether there are no active external or internal
  // requests.
  bool IsEmpty();

 private:
  // Checks whether this object is empty, and if it is and the on_empty callback
  // is set, calls it.
  void CheckEmpty();

  const std::string ledger_name_;
  const storage::PageId page_id_;
  PageUsageListener* page_usage_listener_;

  // Stores whether the page was opened by an external request. Used to
  // determine whether to send the OnPageUnused notifications when this is
  // empty.
  bool must_notify_on_page_unused_ = false;
  // Stores whether the page is opened by an external request.
  bool has_external_requests_ = false;
  // Stores the number of active internal requests.
  ssize_t internal_request_count_ = 0;

  fit::closure on_empty_callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageConnectionNotifier);
};

PageConnectionNotifier::PageConnectionNotifier(
    std::string ledger_name, storage::PageId page_id,
    PageUsageListener* page_usage_listener)
    : ledger_name_(std::move(ledger_name)),
      page_id_(std::move(page_id)),
      page_usage_listener_(page_usage_listener) {}

PageConnectionNotifier::~PageConnectionNotifier() {}

void PageConnectionNotifier::RegisterExternalRequest() {
  if (has_external_requests_) {
    return;
  }
  must_notify_on_page_unused_ = true;
  has_external_requests_ = true;
  page_usage_listener_->OnPageOpened(ledger_name_, page_id_);
}

void PageConnectionNotifier::UnregisterExternalRequests() {
  if (has_external_requests_) {
    page_usage_listener_->OnPageClosed(ledger_name_, page_id_);
    has_external_requests_ = false;
    CheckEmpty();
  }
}

void PageConnectionNotifier::RegisterInternalRequest() {
  ++internal_request_count_;
}

void PageConnectionNotifier::UnregisterInternalRequest() {
  FXL_DCHECK(internal_request_count_ > 0);
  --internal_request_count_;
  CheckEmpty();
}

void PageConnectionNotifier::set_on_empty(fit::closure on_empty_callback) {
  on_empty_callback_ = std::move(on_empty_callback);
}

bool PageConnectionNotifier::IsEmpty() {
  return internal_request_count_ == 0 && !has_external_requests_;
}

void PageConnectionNotifier::CheckEmpty() {
  if (!IsEmpty()) {
    return;
  }
  if (must_notify_on_page_unused_) {
    // We must set |must_notify_on_page_unused_| to false before calling
    // |OnPageUsed|: While |OnPageUsed| is executed it creates an internal
    // request to the PageManagerContainer, hence triggering a call to
    // |UnregisterInternalRequest|. Since that will be the last active request,
    // it will then trigger |CheckEmpty|, reaching again this part of the code.
    // Setting |must_notify_on_page_unused_| to false prevents this infinite
    // loop.
    must_notify_on_page_unused_ = false;
    page_usage_listener_->OnPageUnused(ledger_name_, page_id_);
  }
  if (on_empty_callback_) {
    on_empty_callback_();
  }
}

}  // namespace

void LedgerManager::PageAvailabilityManager::MarkPageBusy(
    convert::ExtendedStringView page_id) {
  auto result =
      busy_pages_.emplace(page_id.ToString(), std::vector<fit::closure>());
  FXL_DCHECK(result.second)
      << "Page " << convert::ToHex(page_id) << " is already busy.";
}

void LedgerManager::PageAvailabilityManager::MarkPageAvailable(
    convert::ExtendedStringView page_id) {
  storage::PageId page_id_str = page_id.ToString();
  auto it = busy_pages_.find(page_id_str);
  if (it == busy_pages_.end()) {
    return;
  }

  for (auto& page_callback : it->second) {
    page_callback();
  }
  busy_pages_.erase(it);
}

void LedgerManager::PageAvailabilityManager::OnPageAvailable(
    convert::ExtendedStringView page_id, fit::closure on_page_available) {
  storage::PageId page_id_str = page_id.ToString();
  auto it = busy_pages_.find(page_id_str);
  if (it == busy_pages_.end()) {
    on_page_available();
    return;
  }
  it->second.push_back(std::move(on_page_available));
}

// Container for a PageManager that keeps tracks of in-flight page requests and
// callbacks and fires them when the PageManager is available.
class LedgerManager::PageManagerContainer {
 public:
  PageManagerContainer(std::string ledger_name, storage::PageId page_id,
                       PageUsageListener* page_usage_listener);
  ~PageManagerContainer();

  void set_on_empty(fit::closure on_empty_callback);

  // Keeps track of |page| and |callback|. Binds |page| and fires |callback|
  // when a PageManager is available or an error occurs.
  void BindPage(fidl::InterfaceRequest<Page> page_request,
                fit::function<void(Status)> callback);

  // Keeps track of |page_debug| and |callback|. Binds |page_debug| and fires
  // |callback| when a PageManager is available or an error occurs.
  void BindPageDebug(
      fidl::InterfaceRequest<ledger_internal::PageDebug> page_debug,
      fit::function<void(Status)> callback);

  // Registers a new internal request for PageStorage.
  void NewInternalRequest(
      fit::function<void(Status, ExpiringToken, PageManager*)> callback);

  // Sets the PageManager or the error status for the container. This notifies
  // all awaiting callbacks and binds all pages in case of success.
  void SetPageManager(Status status, std::unique_ptr<PageManager> page_manager);

  // Returns true if there is at least one active external page connection.
  bool PageConnectionIsOpen();

 private:
  // Creates a new ExpiringToken to be used while internal requests for the
  // |PageManager| remain active.
  ExpiringToken NewExpiringToken();

  // Checks whether this container is empty, and calls the |on_empty_callback_|
  // if it is.
  void CheckEmpty();

  const storage::PageId page_id_;
  std::unique_ptr<PageManager> page_manager_;
  PageConnectionNotifier connection_notifier_;
  Status status_ = Status::OK;
  std::vector<std::pair<std::unique_ptr<PageDelayingFacade>,
                        fit::function<void(Status)>>>
      requests_;
  std::vector<std::pair<fidl::InterfaceRequest<ledger_internal::PageDebug>,
                        fit::function<void(Status)>>>
      debug_requests_;
  std::vector<fit::function<void(Status, ExpiringToken, PageManager*)>>
      internal_request_callbacks_;
  bool page_manager_is_set_ = false;
  fit::closure on_empty_callback_;

  // Must be the last member.
  fxl::WeakPtrFactory<PageManagerContainer> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageManagerContainer);
};

LedgerManager::PageManagerContainer::PageManagerContainer(
    std::string ledger_name, storage::PageId page_id,
    PageUsageListener* page_usage_listener)
    : page_id_(page_id),
      connection_notifier_(std::move(ledger_name), std::move(page_id),
                           page_usage_listener),
      weak_factory_(this) {}

LedgerManager::PageManagerContainer::~PageManagerContainer() {
  for (const auto& request : requests_) {
    request.second(Status::INTERNAL_ERROR);
  }
  for (const auto& request : debug_requests_) {
    request.second(Status::INTERNAL_ERROR);
  }
}

void LedgerManager::PageManagerContainer::set_on_empty(
    fit::closure on_empty_callback) {
  on_empty_callback_ = std::move(on_empty_callback);
  connection_notifier_.set_on_empty([this] { CheckEmpty(); });
  if (page_manager_) {
    page_manager_->set_on_empty(
        [this] { connection_notifier_.UnregisterExternalRequests(); });
  }
}

void LedgerManager::PageManagerContainer::BindPage(
    fidl::InterfaceRequest<Page> page_request,
    fit::function<void(Status)> callback) {
  connection_notifier_.RegisterExternalRequest();

  if (status_ != Status::OK) {
    callback(status_);
    return;
  }
  auto delaying_facade =
      std::make_unique<PageDelayingFacade>(page_id_, std::move(page_request));
  if (page_manager_) {
    page_manager_->AddPageDelayingFacade(std::move(delaying_facade),
                                         std::move(callback));
    return;
  }
  requests_.emplace_back(std::move(delaying_facade), std::move(callback));
}

void LedgerManager::PageManagerContainer::BindPageDebug(
    fidl::InterfaceRequest<ledger_internal::PageDebug> page_debug,
    fit::function<void(Status)> callback) {
  connection_notifier_.RegisterExternalRequest();

  if (status_ != Status::OK) {
    callback(status_);
    return;
  }
  if (page_manager_) {
    page_manager_->BindPageDebug(std::move(page_debug), std::move(callback));
    return;
  }
  debug_requests_.emplace_back(std::move(page_debug), std::move(callback));
}

void LedgerManager::PageManagerContainer::NewInternalRequest(
    fit::function<void(Status, ExpiringToken, PageManager*)> callback) {
  if (status_ != Status::OK) {
    callback(status_, fit::defer<fit::closure>([] {}), nullptr);
    return;
  }

  if (page_manager_) {
    callback(status_, NewExpiringToken(), page_manager_.get());
    return;
  }

  internal_request_callbacks_.push_back(std::move(callback));
}

void LedgerManager::PageManagerContainer::SetPageManager(
    Status status, std::unique_ptr<PageManager> page_manager) {
  TRACE_DURATION("ledger", "ledger_manager_set_page_manager");

  FXL_DCHECK(!page_manager_);
  FXL_DCHECK((status != Status::OK) == !page_manager);
  status_ = status;
  page_manager_ = std::move(page_manager);
  page_manager_is_set_ = true;

  for (auto& request : requests_) {
    if (page_manager_) {
      page_manager_->AddPageDelayingFacade(std::move(request.first),
                                           std::move(request.second));
    } else {
      request.second(status_);
    }
  }
  requests_.clear();

  for (auto& request : debug_requests_) {
    if (page_manager_) {
      page_manager_->BindPageDebug(std::move(request.first),
                                   std::move(request.second));
    } else {
      request.second(status_);
    }
  }
  debug_requests_.clear();

  for (auto& callback : internal_request_callbacks_) {
    if (!page_manager_) {
      callback(status_, fit::defer<fit::closure>([] {}), nullptr);
      continue;
    }
    callback(status_, NewExpiringToken(), page_manager_.get());
  }

  if (page_manager_) {
    page_manager_->set_on_empty(
        [this] { connection_notifier_.UnregisterExternalRequests(); });
  } else {
    CheckEmpty();
  }
}

bool LedgerManager::PageManagerContainer::PageConnectionIsOpen() {
  return (page_manager_is_set_ && !page_manager_->IsEmpty()) ||
         !requests_.empty() || !debug_requests_.empty();
}

ExpiringToken LedgerManager::PageManagerContainer::NewExpiringToken() {
  connection_notifier_.RegisterInternalRequest();
  return ExpiringToken(callback::MakeScoped(weak_factory_.GetWeakPtr(), [this] {
    connection_notifier_.UnregisterInternalRequest();
  }));
}

void LedgerManager::PageManagerContainer::CheckEmpty() {
  if (on_empty_callback_ && connection_notifier_.IsEmpty() &&
      page_manager_is_set_ && (!page_manager_ || page_manager_->IsEmpty())) {
    on_empty_callback_();
  }
}

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
      page_usage_listener_(page_usage_listener) {
  bindings_.set_empty_set_handler([this] { CheckEmpty(); });
  page_managers_.set_on_empty([this] { CheckEmpty(); });
  ledger_debug_bindings_.set_empty_set_handler([this] { CheckEmpty(); });
}

LedgerManager::~LedgerManager() {}

void LedgerManager::BindLedger(fidl::InterfaceRequest<Ledger> ledger_request) {
  bindings_.AddBinding(&ledger_impl_, std::move(ledger_request));
}

void LedgerManager::PageIsClosedAndSynced(
    storage::PageIdView page_id,
    fit::function<void(Status, PagePredicateResult)> callback) {
  auto is_synced = [](PageManager* page_manager,
                      fit::function<void(Status, bool)> on_done) {
    page_manager->IsSynced(std::move(on_done));
  };
  PageIsClosedAndSatisfiesPredicate(page_id, std::move(is_synced),
                                    std::move(callback));
}

void LedgerManager::PageIsClosedOfflineAndEmpty(
    storage::PageIdView page_id,
    fit::function<void(Status, PagePredicateResult)> callback) {
  auto is_offline_and_empty = [](PageManager* page_manager,
                                 fit::function<void(Status, bool)> on_done) {
    page_manager->IsOfflineAndEmpty(std::move(on_done));
  };
  PageIsClosedAndSatisfiesPredicate(page_id, std::move(is_offline_and_empty),
                                    std::move(callback));
}

void LedgerManager::DeletePageStorage(convert::ExtendedStringView page_id,
                                      fit::function<void(Status)> callback) {
  auto it = page_managers_.find(page_id);
  if (it != page_managers_.end()) {
    callback(Status::ILLEGAL_STATE);
    return;
  }

  // Block all page requests until deletion is complete.
  page_availability_manager_.MarkPageBusy(page_id);
  storage_->DeletePageStorage(
      page_id, [this, page_id = page_id.ToString(),
                callback = std::move(callback)](storage::Status status) {
        page_availability_manager_.MarkPageAvailable(page_id);
        callback(PageUtils::ConvertStatus(status));
      });
}

void LedgerManager::GetPage(storage::PageIdView page_id, PageState page_state,
                            fidl::InterfaceRequest<Page> page_request,
                            fit::function<void(Status)> callback) {
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
                            page_state](Status status) mutable {
                             // Create the page if it wasn't found.
                             if (status == Status::PAGE_NOT_FOUND) {
                               CreatePageStorage(std::move(page_id), page_state,
                                                 container);
                             }
                           });
}

void LedgerManager::InitPageManagerContainer(
    PageManagerContainer* container, convert::ExtendedStringView page_id,
    fit::function<void(Status)> callback) {
  page_availability_manager_.OnPageAvailable(
      page_id, [this, container, page_id = page_id.ToString(),
                callback = std::move(callback)]() mutable {
        storage_->GetPageStorage(
            std::move(page_id),
            [this, container, callback = std::move(callback)](
                storage::Status storage_status,
                std::unique_ptr<storage::PageStorage> page_storage) mutable {
              Status status =
                  PageUtils::ConvertStatus(storage_status, Status::OK);
              if (status != Status::OK) {
                container->SetPageManager(status, nullptr);
                callback(status);
                return;
              }

              // If the page was found locally, just use it and return.
              if (page_storage) {
                container->SetPageManager(
                    Status::OK,
                    NewPageManager(std::move(page_storage),
                                   PageManager::PageStorageState::AVAILABLE));
                callback(status);
                return;
              }

              callback(Status::PAGE_NOT_FOUND);
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
                container->SetPageManager(Status::INTERNAL_ERROR, nullptr);
                return;
              }
              container->SetPageManager(
                  Status::OK,
                  NewPageManager(
                      std::move(page_storage),
                      page_state == PageState::NEW
                          ? PageManager::PageStorageState::AVAILABLE
                          : PageManager::PageStorageState::NEEDS_SYNC));
            });
      });
}

LedgerManager::PageManagerContainer* LedgerManager::AddPageManagerContainer(
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
    fit::function<void(PageManager*, fit::function<void(Status, bool)>)>
        predicate,
    fit::function<void(Status, PagePredicateResult)> callback) {
  // Start logging whether the page has been opened during the execution of
  // this method.
  uint64_t operation_id = page_was_opened_id_++;
  page_was_opened_map_[page_id.ToString()].push_back(operation_id);
  auto on_return =
      fit::defer([this, page_id = page_id.ToString(), operation_id] {
        RemoveTrackedPage(page_id, operation_id);
      });

  PageManagerContainer* container;

  auto it = page_managers_.find(page_id);
  if (it != page_managers_.end()) {
    // The page manager is open, check if there are any open connections.
    container = &it->second;
    if (container->PageConnectionIsOpen()) {
      callback(Status::OK, PagePredicateResult::PAGE_OPENED);
      return;
    }
  } else {
    // Create a new container and get the PageStorage.
    container = AddPageManagerContainer(page_id);
    InitPageManagerContainer(container, page_id, [container](Status status) {
      if (status == Status::PAGE_NOT_FOUND) {
        container->SetPageManager(status, nullptr);
      }
    });
  }

  container->NewInternalRequest(
      [this, page_id = page_id.ToString(), operation_id,
       predicate = std::move(predicate), on_return = std::move(on_return),
       callback = std::move(callback)](Status status, ExpiringToken token,
                                       PageManager* page_manager) mutable {
        auto final_callback =
            [token = std::move(token), callback = std::move(callback)](
                Status status, PagePredicateResult result) mutable {
              // The token needs to be valid while |predicate| is being
              // computed. Invalidate it right before calling the callback.
              token.call();
              callback(status, result);
            };
        if (status != Status::OK) {
          final_callback(status, PagePredicateResult::PAGE_OPENED);
          return;
        }
        FXL_DCHECK(page_manager);
        predicate(page_manager, [this, page_id = std::move(page_id),
                                 operation_id, on_return = std::move(on_return),
                                 callback = std::move(final_callback)](
                                    Status status, bool condition) mutable {
          on_return.cancel();
          if (!RemoveTrackedPage(page_id, operation_id) ||
              status != Status::OK) {
            // If |RemoveTrackedPage| returns false, this means that
            // the page was opened during this operation and
            // |PAGE_OPENED| must be returned.
            callback(status, PagePredicateResult::PAGE_OPENED);
            return;
          }
          callback(Status::OK, condition ? PagePredicateResult::YES
                                         : PagePredicateResult::NO);
        });
      });
}

bool LedgerManager::RemoveTrackedPage(storage::PageIdView page_id,
                                      uint64_t operation_id) {
  auto it = page_was_opened_map_.find(page_id.ToString());
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
}

void LedgerManager::MaybeMarkPageOpened(storage::PageIdView page_id) {
  page_was_opened_map_.erase(page_id.ToString());
}

void LedgerManager::CheckEmpty() {
  if (on_empty_callback_ && bindings_.size() == 0 && page_managers_.empty() &&
      ledger_debug_bindings_.size() == 0)
    on_empty_callback_();
}

void LedgerManager::SetConflictResolverFactory(
    fidl::InterfaceHandle<ConflictResolverFactory> factory) {
  merge_manager_.AddFactory(std::move(factory));
}

void LedgerManager::BindLedgerDebug(
    fidl::InterfaceRequest<LedgerDebug> request) {
  ledger_debug_bindings_.AddBinding(this, std::move(request));
}

// TODO(ayaelattar): See LE-370: Inspect ledgers and pages not currently active.
void LedgerManager::GetPagesList(GetPagesListCallback callback) {
  fidl::VectorPtr<PageId> result;
  result.resize(0);
  for (const auto& key_value : page_managers_) {
    PageId page_id;
    convert::ToArray(key_value.first, &page_id.id);
    result.push_back(page_id);
  }
  callback(std::move(result));
}

void LedgerManager::GetPageDebug(
    PageId page_id,
    fidl::InterfaceRequest<ledger_internal::PageDebug> page_debug,
    GetPageDebugCallback callback) {
  MaybeMarkPageOpened(page_id.id);
  auto it = page_managers_.find(convert::ExtendedStringView(page_id.id));
  if (it != page_managers_.end()) {
    it->second.BindPageDebug(std::move(page_debug), std::move(callback));
  } else {
    callback(Status::PAGE_NOT_FOUND);
  }
}

}  // namespace ledger
