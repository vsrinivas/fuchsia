// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/ledger_manager.h"

#include <string>
#include <utility>
#include <vector>

#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "peridot/bin/ledger/app/constants.h"
#include "peridot/bin/ledger/app/page_utils.h"
#include "peridot/bin/ledger/glue/crypto/rand.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"

namespace ledger {

// Container for a PageManager that keeps tracks of in-flight page requests and
// callbacks and fires them when the PageManager is available.
class LedgerManager::PageManagerContainer {
 public:
  PageManagerContainer() {}
  ~PageManagerContainer() {
    for (const auto& request : requests_) {
      request.second(Status::INTERNAL_ERROR);
    }
  }

  void set_on_empty(const fxl::Closure& on_empty_callback) {
    on_empty_callback_ = on_empty_callback;
    if (page_manager_) {
      page_manager_->set_on_empty(on_empty_callback);
    }
  };

  // Keeps track of |page| and |callback|. Binds |page| and fires |callback|
  // when a PageManager is available or an error occurs.
  void BindPage(fidl::InterfaceRequest<Page> page_request,
                std::function<void(Status)> callback) {
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

  // Sets the PageManager or the error status for the container. This notifies
  // all awaiting callbacks and binds all pages in case of success.
  void SetPageManager(Status status,
                      std::unique_ptr<PageManager> page_manager) {
    FXL_DCHECK(!page_manager_);
    FXL_DCHECK((status != Status::OK) == !page_manager);
    status_ = status;
    page_manager_ = std::move(page_manager);
    for (auto& request : requests_) {
      if (page_manager_) {
        page_manager_->BindPage(std::move(request.first),
                                std::move(request.second));
      } else {
        request.second(status_);
      }
    }
    requests_.clear();
    if (on_empty_callback_) {
      if (page_manager_) {
        page_manager_->set_on_empty(on_empty_callback_);
      } else {
        on_empty_callback_();
      }
    }
  }

 private:
  std::unique_ptr<PageManager> page_manager_;
  Status status_ = Status::OK;
  std::vector<
      std::pair<fidl::InterfaceRequest<Page>, std::function<void(Status)>>>
      requests_;
  fxl::Closure on_empty_callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageManagerContainer);
};

LedgerManager::LedgerManager(Environment* environment,
                             std::unique_ptr<storage::LedgerStorage> storage,
                             std::unique_ptr<cloud_sync::LedgerSync> sync)
    : environment_(environment),
      storage_(std::move(storage)),
      sync_(std::move(sync)),
      ledger_impl_(this),
      merge_manager_(environment_) {}

LedgerManager::~LedgerManager() {}

void LedgerManager::BindLedger(fidl::InterfaceRequest<Ledger> ledger_request) {
  bindings_.AddBinding(&ledger_impl_, std::move(ledger_request));
}

void LedgerManager::GetPage(convert::ExtendedStringView page_id,
                            fidl::InterfaceRequest<Page> page_request,
                            std::function<void(Status)> callback) {
  // If we have the page manager ready, just ask for a new page impl.
  auto it = page_managers_.find(page_id);
  if (it != page_managers_.end()) {
    it->second.BindPage(std::move(page_request), std::move(callback));
    return;
  }

  PageManagerContainer* container = AddPageManagerContainer(page_id);
  container->BindPage(std::move(page_request), std::move(callback));

  storage_->GetPageStorage(
      page_id.ToString(),
      [ this, page_id = page_id.ToString(), container ](
          storage::Status storage_status,
          std::unique_ptr<storage::PageStorage> page_storage) mutable {
        Status status = PageUtils::ConvertStatus(storage_status, Status::OK);
        if (status != Status::OK) {
          container->SetPageManager(status, nullptr);
          return;
        }

        // If the page was found locally, just use it and return.
        if (page_storage) {
          container->SetPageManager(
              Status::OK,
              NewPageManager(std::move(page_storage),
                             PageManager::PageStorageState::EXISTING));
          return;
        }

        // If the page was not found locally, create it.
        CreatePageStorage(std::move(page_id), container);
        return;
      });
}

Status LedgerManager::DeletePage(convert::ExtendedStringView page_id) {
  auto it = page_managers_.find(page_id);
  if (it != page_managers_.end()) {
    page_managers_.erase(it);
  }

  if (storage_->DeletePageStorage(page_id)) {
    return Status::OK;
  }
  return Status::PAGE_NOT_FOUND;
}

void LedgerManager::CreatePageStorage(storage::PageId page_id,
                                      PageManagerContainer* container) {
  storage_->CreatePageStorage(
      page_id,
      [this, container](storage::Status status,
                        std::unique_ptr<storage::PageStorage> page_storage) {
        if (status != storage::Status::OK) {
          container->SetPageManager(Status::INTERNAL_ERROR, nullptr);
          return;
        }
        container->SetPageManager(
            Status::OK, NewPageManager(std::move(page_storage),
                                       PageManager::PageStorageState::NEW));
      });
}

LedgerManager::PageManagerContainer* LedgerManager::AddPageManagerContainer(
    storage::PageIdView page_id) {
  auto ret = page_managers_.emplace(std::piecewise_construct,
                                    std::forward_as_tuple(page_id.ToString()),
                                    std::forward_as_tuple());
  FXL_DCHECK(ret.second);
  return &ret.first->second;
}

std::unique_ptr<PageManager> LedgerManager::NewPageManager(
    std::unique_ptr<storage::PageStorage> page_storage,
    PageManager::PageStorageState state) {
  std::unique_ptr<cloud_sync::PageSyncContext> page_sync_context;
  if (sync_) {
    page_sync_context = sync_->CreatePageContext(page_storage.get(), [] {
      // TODO(ppi): reinitialize the sync?
      FXL_LOG(ERROR) << "Page Sync stopped due to unrecoverable error.";
    });
  }
  return std::make_unique<PageManager>(
      environment_, std::move(page_storage), std::move(page_sync_context),
      merge_manager_.GetMergeResolver(page_storage.get()), state);
}

void LedgerManager::CheckEmpty() {
  if (!on_empty_callback_)
    return;
  if (bindings_.size() == 0 && page_managers_.empty())
    on_empty_callback_();
}

void LedgerManager::SetConflictResolverFactory(
    fidl::InterfaceHandle<ConflictResolverFactory> factory) {
  merge_manager_.SetFactory(std::move(factory));
}

}  // namespace ledger
