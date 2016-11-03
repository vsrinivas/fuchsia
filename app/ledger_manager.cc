// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/app/ledger_manager.h"

#include "apps/ledger/app/constants.h"
#include "apps/ledger/app/page_utils.h"
#include "apps/ledger/glue/crypto/rand.h"
#include "apps/ledger/storage/public/page_storage.h"
#include "lib/ftl/logging.h"
#include "mojo/public/cpp/bindings/interface_request.h"

namespace ledger {

namespace {

storage::PageId RandomId() {
  std::string result;
  result.resize(kPageIdSize);
  glue::RandBytes(&result[0], kPageIdSize);
  return result;
}

}  // namespace

// Container for a PageManager that keeps tracks of in-flight callbacks and
// fires them when the PageManager is available.
class LedgerManager::PageManagerContainer {
 public:
  PageManagerContainer() : status_(Status::OK) {}
  ~PageManagerContainer() {
    for (const auto& callback : callbacks_) {
      callback(Status::INTERNAL_ERROR, nullptr);
    }
  }

  // Keeps track of |callback| and fires it when a PageManager is available or
  // an error occurs.
  void GetPage(std::function<void(Status, PagePtr)>&& callback) {
    if (status_ != Status::OK) {
      callback(status_, nullptr);
      return;
    }
    if (page_manager_) {
      callback(status_, page_manager_->GetPagePtr());
      return;
    }
    callbacks_.push_back(std::move(callback));
  }

  // Sets the PageManager or the error status for the container. This notifies
  // all awaiting callbacks.
  void SetPageManager(Status status,
                      std::unique_ptr<PageManager> page_manager) {
    FTL_DCHECK(status != Status::OK || page_manager);
    status_ = status;
    page_manager_ = std::move(page_manager);
    for (const auto& callback : callbacks_) {
      callback(status_, page_manager_ ? page_manager_->GetPagePtr() : nullptr);
    }
    callbacks_.clear();
  }

 private:
  std::unique_ptr<PageManager> page_manager_;
  Status status_;
  std::vector<std::function<void(Status, PagePtr)>> callbacks_;

  FTL_DISALLOW_COPY_AND_ASSIGN(PageManagerContainer);
};

LedgerManager::LedgerManager(std::unique_ptr<storage::LedgerStorage> storage)
    : storage_(std::move(storage)), ledger_impl_(this) {}

LedgerManager::~LedgerManager() {}

LedgerPtr LedgerManager::GetLedgerPtr() {
  LedgerPtr ledger;
  bindings_.AddBinding(&ledger_impl_, GetProxy(&ledger));
  return ledger;
}

void LedgerManager::CreatePage(std::function<void(Status, PagePtr)> callback) {
  storage::PageId page_id = RandomId();
  std::unique_ptr<storage::PageStorage> page_storage;

  storage::Status status = storage_->CreatePageStorage(page_id, &page_storage);
  if (status != storage::Status::OK) {
    callback(Status::INTERNAL_ERROR, nullptr);
    return;
  }

  PageManagerContainer* container = AddPageManagerContainer(page_id);
  container->SetPageManager(
      Status::OK, NewPageManager(std::move(page_id), std::move(page_storage)));
  container->GetPage(std::move(callback));
}

void LedgerManager::GetPage(convert::ExtendedStringView page_id,
                            CreateIfNotFound create_if_not_found,
                            std::function<void(Status, PagePtr)> callback) {
  // If we have the page manager ready, just ask for a new page impl.
  auto it = page_managers_.find(page_id);
  if (it != page_managers_.end()) {
    it->second->GetPage(std::move(callback));
    return;
  }

  PageManagerContainer* container = AddPageManagerContainer(page_id);
  container->GetPage(std::move(callback));

  storage_->GetPageStorage(
      page_id,
      [ this, create_if_not_found, page_id = page_id.ToString(), container ](
          storage::Status storage_status,
          std::unique_ptr<storage::PageStorage> page_storage) mutable {
        Status status = PageUtils::ConvertStatus(storage_status, Status::OK);
        if (status != Status::OK) {
          container->SetPageManager(status, nullptr);
          return;
        }
        if (!page_storage) {
          if (create_if_not_found == CreateIfNotFound::NO) {
            container->SetPageManager(Status::PAGE_NOT_FOUND, nullptr);
            return;
          }
          storage::Status status =
              storage_->CreatePageStorage(page_id, &page_storage);
          if (status != storage::Status::OK) {
            container->SetPageManager(Status::INTERNAL_ERROR, nullptr);
            return;
          }
        }

        container->SetPageManager(
            Status::OK,
            NewPageManager(std::move(page_id), std::move(page_storage)));
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

LedgerManager::PageManagerContainer* LedgerManager::AddPageManagerContainer(
    storage::PageIdView page_id) {
  auto ret = page_managers_.insert(std::make_pair(
      page_id.ToString(), std::make_unique<PageManagerContainer>()));
  FTL_DCHECK(ret.second);
  return ret.first->second.get();
}

std::unique_ptr<PageManager> LedgerManager::NewPageManager(
    storage::PageId&& page_id,
    std::unique_ptr<storage::PageStorage> page_storage) {
  return std::make_unique<PageManager>(std::move(page_storage),
                                       [ this, page_id = std::move(page_id) ] {
                                         page_managers_.erase(page_id);
                                       });
}

}  // namespace ledger
