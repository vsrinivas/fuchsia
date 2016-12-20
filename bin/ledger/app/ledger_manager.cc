// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/ledger_manager.h"

#include <string>
#include <utility>
#include <vector>

#include "apps/ledger/src/app/constants.h"
#include "apps/ledger/src/app/page_utils.h"
#include "apps/ledger/src/glue/crypto/rand.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"

namespace ledger {

namespace {

storage::PageId RandomId() {
  std::string result;
  result.resize(kPageIdSize);
  glue::RandBytes(&result[0], kPageIdSize);
  return result;
}

}  // namespace

// Container for a PageManager that keeps tracks of in-flight page requests and
// callbacks and fires them when the PageManager is available.
class LedgerManager::PageManagerContainer {
 public:
  PageManagerContainer() : status_(Status::OK) {}
  ~PageManagerContainer() {
    for (const auto& request : requests_) {
      request.second(Status::INTERNAL_ERROR);
    }
  }

  void set_on_empty(const ftl::Closure& on_empty_callback) {
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
      page_manager_->BindPage(std::move(page_request));
      callback(status_);
      return;
    }
    requests_.push_back(
        std::make_pair(std::move(page_request), std::move(callback)));
  }

  // Sets the PageManager or the error status for the container. This notifies
  // all awaiting callbacks and binds all pages in case of success.
  void SetPageManager(Status status,
                      std::unique_ptr<PageManager> page_manager) {
    FTL_DCHECK(!page_manager_);
    FTL_DCHECK(status != Status::OK || page_manager);
    status_ = status;
    page_manager_ = std::move(page_manager);
    for (auto it = requests_.begin(); it != requests_.end(); ++it) {
      if (page_manager_)
        page_manager_->BindPage(std::move(it->first));
      it->second(status_);
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
  Status status_;
  std::vector<
      std::pair<fidl::InterfaceRequest<Page>, std::function<void(Status)>>>
      requests_;
  ftl::Closure on_empty_callback_;

  FTL_DISALLOW_COPY_AND_ASSIGN(PageManagerContainer);
};

LedgerManager::LedgerManager(std::unique_ptr<storage::LedgerStorage> storage,
                             std::unique_ptr<cloud_sync::LedgerSync> sync)
    : storage_(std::move(storage)),
      sync_(std::move(sync)),
      ledger_impl_(this) {}

LedgerManager::~LedgerManager() {}

void LedgerManager::BindLedger(fidl::InterfaceRequest<Ledger> ledger_request) {
  bindings_.AddBinding(&ledger_impl_, std::move(ledger_request));
}

void LedgerManager::CreatePage(fidl::InterfaceRequest<Page> page_request,
                               std::function<void(Status)> callback) {
  storage::PageId page_id = RandomId();
  std::unique_ptr<storage::PageStorage> page_storage;

  storage::Status status = storage_->CreatePageStorage(page_id, &page_storage);
  if (status != storage::Status::OK) {
    callback(Status::INTERNAL_ERROR);
    return;
  }

  PageManagerContainer* container = AddPageManagerContainer(page_id);
  container->SetPageManager(Status::OK,
                            NewPageManager(std::move(page_storage)));
  container->BindPage(std::move(page_request), std::move(callback));
}

void LedgerManager::GetPage(convert::ExtendedStringView page_id,
                            CreateIfNotFound create_if_not_found,
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
      [ this, create_if_not_found, page_id = page_id.ToString(), container ](
          storage::Status storage_status,
          std::unique_ptr<storage::PageStorage> page_storage) mutable {
        Status status = PageUtils::ConvertStatus(storage_status, Status::OK);
        if (status != Status::OK) {
          container->SetPageManager(status, nullptr);
          return;
        }

        // If the page was found locally, just use it and return.
        if (page_storage) {
          container->SetPageManager(Status::OK,
                                    NewPageManager(std::move(page_storage)));
          return;
        }

        // If the page was not found locally, but it doesn't matter whether it
        // exists in the cloud (because it will be created anyway), don't bother
        // asking the cloud.
        if (create_if_not_found == CreateIfNotFound::YES || !sync_) {
          HandleGetPage(std::move(page_id),
                        cloud_sync::RemoteResponse::NOT_FOUND,
                        create_if_not_found, container);
          return;
        }

        // See if the page exists in the cloud.
        sync_->RemoteContains(page_id, [
          this, page_id = std::move(page_id), create_if_not_found, container
        ](cloud_sync::RemoteResponse remote_response) {
          HandleGetPage(std::move(page_id), remote_response,
                        create_if_not_found, container);
        });
      });
}

Status LedgerManager::DeletePage(convert::ExtendedStringView page_id) {
  auto it = page_managers_.find(page_id);
  if (it != page_managers_.end()) {
    page_managers_.erase(it);
  }

  if (storage_->DeletePageStorage(page_id)) {
    return Status::OK;
  } else {
    return Status::PAGE_NOT_FOUND;
  }
}

void LedgerManager::HandleGetPage(storage::PageId page_id,
                                  cloud_sync::RemoteResponse remote_response,
                                  CreateIfNotFound create_if_not_found,
                                  PageManagerContainer* container) {
  if (remote_response == cloud_sync::RemoteResponse::NOT_FOUND &&
      create_if_not_found == CreateIfNotFound::NO) {
    container->SetPageManager(Status::PAGE_NOT_FOUND, nullptr);
    return;
  }

  if (remote_response != cloud_sync::RemoteResponse::FOUND &&
      remote_response != cloud_sync::RemoteResponse::NOT_FOUND) {
    // Remote response was one of the error responses - might be a network
    // error, might be a server error; in any case we we can't verify if the
    // remote page exists or not. In this case we still create the empty local
    // page. This is sometimes useful, as when the connection is back, the data
    // will sync automagically. TODO(ppi): expose a richer signal to the client.
    FTL_LOG(WARNING) << "Failed to verify if the page exists in the cloud, "
                     << "assuming yes and binding to an empty page.";
  }

  std::unique_ptr<storage::PageStorage> page_storage;
  storage::Status status = storage_->CreatePageStorage(page_id, &page_storage);
  if (status != storage::Status::OK) {
    container->SetPageManager(Status::INTERNAL_ERROR, nullptr);
    return;
  }
  container->SetPageManager(Status::OK,
                            NewPageManager(std::move(page_storage)));
}

LedgerManager::PageManagerContainer* LedgerManager::AddPageManagerContainer(
    storage::PageIdView page_id) {
  auto ret = page_managers_.emplace(std::piecewise_construct,
                                    std::forward_as_tuple(page_id.ToString()),
                                    std::forward_as_tuple());
  FTL_DCHECK(ret.second);
  return &ret.first->second;
}

std::unique_ptr<PageManager> LedgerManager::NewPageManager(
    std::unique_ptr<storage::PageStorage> page_storage) {
  std::unique_ptr<cloud_sync::PageSyncContext> page_sync_context;
  if (sync_) {
    page_sync_context = sync_->CreatePageContext(page_storage.get(), [] {
      // TODO(ppi): reinitialize the sync?
      FTL_LOG(ERROR) << "Page Sync stopped due to unrecoverable error.";
    });
  }
  return std::make_unique<PageManager>(
      std::move(page_storage), std::move(page_sync_context),
      merge_manager_.GetMergeResolver(page_storage.get()));
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
