// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/ledger_manager.h"

#include <string>
#include <utility>
#include <vector>

#include <lib/fit/function.h>
#include <trace/event.h>

#include "lib/fidl/cpp/interface_request.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "peridot/bin/ledger/app/constants.h"
#include "peridot/bin/ledger/app/page_eviction_manager.h"
#include "peridot/bin/ledger/app/page_utils.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/p2p_sync/public/page_communicator.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"

namespace ledger {

// A token that performs a given action on destruction.
// ExpiringToken objects are used with internal page requests to notify the
// PageManagerContainer that the requested PageManager is no longer used.
using ExpiringToken = fxl::AutoCall<fit::closure>;

// Container for a PageManager that keeps tracks of in-flight page requests and
// callbacks and fires them when the PageManager is available.
class LedgerManager::PageManagerContainer {
 public:
  PageManagerContainer(std::string ledger_name, storage::PageId page_id,
                       PageUsageListener* page_usage_listener)
      : ledger_name_(ledger_name),
        page_id_(page_id),
        page_usage_listener_(page_usage_listener),
        weak_factory_(this) {}

  ~PageManagerContainer() {
    for (const auto& request : requests_) {
      request.second(Status::INTERNAL_ERROR);
    }
    for (const auto& request : debug_requests_) {
      request.second(Status::INTERNAL_ERROR);
    }
    page_usage_listener_->OnPageClosed(ledger_name_, page_id_);
  }

  void set_on_empty(const fxl::Closure& on_empty_callback) {
    on_empty_callback_ = on_empty_callback;
    if (page_manager_) {
      page_manager_->set_on_empty([this] { CheckEmpty(); });
    }
  };

  // Keeps track of |page| and |callback|. Binds |page| and fires |callback|
  // when a PageManager is available or an error occurs.
  void BindPage(fidl::InterfaceRequest<Page> page_request,
                std::function<void(Status)> callback) {
    MaybeNotifyUsageListener();

    if (status_ != Status::OK) {
      callback(status_);
      return;
    }
    if (page_manager_) {
      page_manager_->BindPage(std::move(page_request), std::move(callback));
      return;
    }
    requests_.emplace_back(std::move(page_request), std::move(callback));
  }

  // Keeps track of |page_debug| and |callback|. Binds |page_debug| and fires
  // |callback| when a PageManager is available or an error occurs.
  void BindPageDebug(
      fidl::InterfaceRequest<ledger_internal::PageDebug> page_debug,
      std::function<void(Status)> callback) {
    MaybeNotifyUsageListener();

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

  // Registers a new internal request for PageStorage.
  void NewInternalRequest(
      std::function<void(Status, ExpiringToken, PageManager*)> callback) {
    if (status_ != Status::OK) {
      callback(status_, fxl::MakeAutoCall<fit::closure>([] {}), nullptr);
      return;
    }

    if (page_manager_) {
      callback(status_, NewExpiringToken(), page_manager_.get());
      return;
    }

    internal_request_callbacks_.push_back(std::move(callback));
  }

  // Sets the PageManager or the error status for the container. This notifies
  // all awaiting callbacks and binds all pages in case of success.
  void SetPageManager(Status status,
                      std::unique_ptr<PageManager> page_manager) {
    TRACE_DURATION("ledger", "ledger_manager_set_page_manager");

    FXL_DCHECK(!page_manager_);
    FXL_DCHECK((status != Status::OK) == !page_manager);
    status_ = status;
    page_manager_ = std::move(page_manager);
    page_manager_is_set_ = true;

    for (auto& request : requests_) {
      if (page_manager_) {
        page_manager_->BindPage(std::move(request.first),
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
        callback(status_, fxl::MakeAutoCall<fit::closure>([] {}), nullptr);
        continue;
      }
      callback(status_, NewExpiringToken(), page_manager_.get());
    }

    if (page_manager_) {
      page_manager_->set_on_empty([this] { CheckEmpty(); });
    } else {
      CheckEmpty();
    }
  }

  bool PageConnectionIsOpen() {
    return (page_manager_is_set_ && !page_manager_->IsEmpty()) ||
           !requests_.empty() || !debug_requests_.empty();
  }

 private:
  // Notifies the PageUsageListener on the page being opened the first time it
  // is called. |MaybeNotifyUsageListener| should only be called after external
  // requests for the PageManager.
  void MaybeNotifyUsageListener() {
    if (!page_opened_notification_sent_) {
      page_usage_listener_->OnPageOpened(ledger_name_, page_id_);
      page_opened_notification_sent_ = true;
    }
  }

  ExpiringToken NewExpiringToken() {
    ++internal_request_count_;
    return ExpiringToken(
        callback::MakeScoped(weak_factory_.GetWeakPtr(), [this] {
          --internal_request_count_;
          CheckEmpty();
        }));
  }

  void CheckEmpty() {
    if (on_empty_callback_ && internal_request_count_ == 0 &&
        page_manager_is_set_ && (!page_manager_ || page_manager_->IsEmpty())) {
      on_empty_callback_();
    }
  }

  const std::string ledger_name_;
  const storage::PageId page_id_;
  std::unique_ptr<PageManager> page_manager_;
  PageUsageListener* page_usage_listener_;
  Status status_ = Status::OK;
  std::vector<
      std::pair<fidl::InterfaceRequest<Page>, std::function<void(Status)>>>
      requests_;
  std::vector<std::pair<fidl::InterfaceRequest<ledger_internal::PageDebug>,
                        std::function<void(Status)>>>
      debug_requests_;
  ssize_t internal_request_count_ = 0;
  std::vector<std::function<void(Status, ExpiringToken, PageManager*)>>
      internal_request_callbacks_;
  // Stores whether PageUsageListener was notified about the page being opened.
  bool page_opened_notification_sent_ = false;
  bool page_manager_is_set_ = false;
  fxl::Closure on_empty_callback_;

  // Must be the last member.
  fxl::WeakPtrFactory<PageManagerContainer> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageManagerContainer);
};

LedgerManager::LedgerManager(
    Environment* environment, std::string ledger_name,
    std::unique_ptr<encryption::EncryptionService> encryption_service,
    std::unique_ptr<storage::LedgerStorage> storage,
    std::unique_ptr<sync_coordinator::LedgerSync> ledger_sync,
    PageUsageListener* page_usage_listener)
    : environment_(environment),
      ledger_name_(ledger_name),
      encryption_service_(std::move(encryption_service)),
      storage_(std::move(storage)),
      ledger_sync_(std::move(ledger_sync)),
      ledger_impl_(this),
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
    std::function<void(Status, PageClosedAndSynced)> callback) {
  // Check if there was a previous call to this function for the same page that
  // hasn't terminated.
  // TODO(nellyv): Add support for two concurrent calls for the same page. See
  // LE-500.
  if (page_was_opened_map_.find(page_id.ToString()) !=
      page_was_opened_map_.end()) {
    callback(Status::OK, PageClosedAndSynced::UNKNOWN);
    return;
  }

  // Start logging whether the page has been opened during the execution of
  // this method.
  page_was_opened_map_[page_id.ToString()] = false;
  auto on_return = fxl::MakeAutoCall([this, page_id = page_id.ToString()] {
    auto it = page_was_opened_map_.find(page_id);
    FXL_DCHECK(it != page_was_opened_map_.end());
    page_was_opened_map_.erase(it);
  });

  PageManagerContainer* container;

  auto it = page_managers_.find(page_id);
  if (it != page_managers_.end()) {
    // The page manager is open, check if there are any open connections.
    container = &it->second;
    if (container->PageConnectionIsOpen()) {
      callback(Status::OK, PageClosedAndSynced::NO);
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

  container->NewInternalRequest(fxl::MakeCopyable(
      [this, page_id = page_id.ToString(), on_return = std::move(on_return),
       callback = std::move(callback)](Status status, ExpiringToken token,
                                       PageManager* page_manager) mutable {
        if (status != Status::OK) {
          callback(status, PageClosedAndSynced::UNKNOWN);
          return;
        }
        FXL_DCHECK(page_manager);
        page_manager->IsSynced(fxl::MakeCopyable(
            [this, page_id = std::move(page_id),
             on_return = std::move(on_return), token = std::move(token),
             callback = std::move(callback)](Status status, bool is_synced) {
              if (status != Status::OK || page_was_opened_map_[page_id]) {
                callback(status, PageClosedAndSynced::UNKNOWN);
                return;
              }
              callback(Status::OK, is_synced ? PageClosedAndSynced::YES
                                             : PageClosedAndSynced::NO);
            }));
      }));
}

void LedgerManager::GetPage(storage::PageIdView page_id, PageState page_state,
                            fidl::InterfaceRequest<Page> page_request,
                            std::function<void(Status)> callback) {
  MaybeMarkPageOpened(page_id);

  // If we have the page manager ready, just bind the request and return.
  auto it = page_managers_.find(page_id);
  if (it != page_managers_.end()) {
    it->second.BindPage(std::move(page_request), std::move(callback));
    return;
  }

  PageManagerContainer* container = AddPageManagerContainer(page_id);
  container->BindPage(std::move(page_request), std::move(callback));

  InitPageManagerContainer(container, page_id,
                           [this, container, page_id = page_id.ToString(),
                            page_state](Status status) {
                             // Create the page if it wasn't found.
                             if (status == Status::PAGE_NOT_FOUND) {
                               CreatePageStorage(std::move(page_id), page_state,
                                                 container);
                             }
                           });
}

Status LedgerManager::DeletePage(convert::ExtendedStringView page_id) {
  FXL_NOTIMPLEMENTED();
  return Status::INTERNAL_ERROR;
}

void LedgerManager::InitPageManagerContainer(
    PageManagerContainer* container, convert::ExtendedStringView page_id,
    std::function<void(Status)> callback) {
  storage_->GetPageStorage(
      page_id.ToString(),
      [this, container, page_id = page_id.ToString(),
       callback = std::move(callback)](
          storage::Status storage_status,
          std::unique_ptr<storage::PageStorage> page_storage) mutable {
        Status status = PageUtils::ConvertStatus(storage_status, Status::OK);
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
}

void LedgerManager::CreatePageStorage(storage::PageId page_id,
                                      PageState page_state,
                                      PageManagerContainer* container) {
  storage_->CreatePageStorage(
      page_id, [this, page_state, container](
                   storage::Status status,
                   std::unique_ptr<storage::PageStorage> page_storage) {
        if (status != storage::Status::OK) {
          container->SetPageManager(Status::INTERNAL_ERROR, nullptr);
          return;
        }
        container->SetPageManager(
            Status::OK,
            NewPageManager(std::move(page_storage),
                           page_state == PageState::NEW
                               ? PageManager::PageStorageState::AVAILABLE
                               : PageManager::PageStorageState::NEEDS_SYNC));
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
    page_sync = ledger_sync_->CreatePageSync(
        page_storage.get(), page_storage.get(), [] {
          // TODO(ppi): reinitialize the sync?
          FXL_LOG(ERROR) << "Page Sync stopped due to unrecoverable error.";
        });
  }
  return std::make_unique<PageManager>(
      environment_, std::move(page_storage), std::move(page_sync),
      merge_manager_.GetMergeResolver(page_storage.get()), state);
}

void LedgerManager::MaybeMarkPageOpened(storage::PageIdView page_id) {
  auto it = page_was_opened_map_.find(page_id.ToString());
  if (it != page_was_opened_map_.end()) {
    it->second = true;
  }
}

void LedgerManager::CheckEmpty() {
  if (on_empty_callback_ && bindings_.size() == 0 && page_managers_.empty() &&
      ledger_debug_bindings_.size() == 0)
    on_empty_callback_();
}

void LedgerManager::SetConflictResolverFactory(
    fidl::InterfaceHandle<ConflictResolverFactory> factory) {
  merge_manager_.SetFactory(std::move(factory));
}

void LedgerManager::BindLedgerDebug(
    fidl::InterfaceRequest<LedgerDebug> request) {
  ledger_debug_bindings_.AddBinding(this, std::move(request));
}

// TODO(ayaelattar): See LE-370: Inspect ledgers and pages not currently active.
void LedgerManager::GetPagesList(GetPagesListCallback callback) {
  fidl::VectorPtr<ledger::PageId> result;
  result.resize(0);
  for (const auto& key_value : page_managers_) {
    ledger::PageId page_id;
    convert::ToArray(key_value.first, &page_id.id);
    result.push_back(page_id);
  }
  callback(std::move(result));
}

void LedgerManager::GetPageDebug(
    ledger::PageId page_id,
    fidl::InterfaceRequest<ledger_internal::PageDebug> page_debug,
    GetPageDebugCallback callback) {
  MaybeMarkPageOpened(page_id.id);
  auto it = page_managers_.find(convert::ExtendedStringView(page_id.id));
  if (it != page_managers_.end()) {
    it->second.BindPageDebug(std::move(page_debug), callback);
  } else {
    callback(Status::PAGE_NOT_FOUND);
  }
}

}  // namespace ledger
