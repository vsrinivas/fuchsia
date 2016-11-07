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

  // Keeps track of |page| and |callback|. Binds |page| and fires |callback|
  // when a PageManager is available or an error occurs.
  void BindPage(mojo::InterfaceRequest<Page> page_request,
                std::function<void(Status)>&& callback) {
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
    FTL_DCHECK(status != Status::OK || page_manager);
    status_ = status;
    page_manager_ = std::move(page_manager);
    for (auto it = requests_.begin(); it != requests_.end(); ++it) {
      if (page_manager_)
        page_manager_->BindPage(std::move(it->first));
      it->second(status_);
    }
    requests_.clear();
  }

 private:
  std::unique_ptr<PageManager> page_manager_;
  Status status_;
  std::vector<
      std::pair<mojo::InterfaceRequest<Page>, std::function<void(Status)>>>
      requests_;

  FTL_DISALLOW_COPY_AND_ASSIGN(PageManagerContainer);
};

LedgerManager::LedgerManager(std::unique_ptr<storage::LedgerStorage> storage)
    : storage_(std::move(storage)), ledger_impl_(this) {}

LedgerManager::~LedgerManager() {}

void LedgerManager::BindLedger(mojo::InterfaceRequest<Ledger> ledger_request) {
  bindings_.AddBinding(&ledger_impl_, std::move(ledger_request));
}

void LedgerManager::CreatePage(mojo::InterfaceRequest<Page> page_request,
                               std::function<void(Status)> callback) {
  storage::PageId page_id = RandomId();
  std::unique_ptr<storage::PageStorage> page_storage;

  storage::Status status = storage_->CreatePageStorage(page_id, &page_storage);
  if (status != storage::Status::OK) {
    callback(Status::INTERNAL_ERROR);
    return;
  }

  PageManagerContainer* container = AddPageManagerContainer(page_id);
  container->SetPageManager(
      Status::OK, NewPageManager(std::move(page_id), std::move(page_storage)));
  container->BindPage(std::move(page_request), std::move(callback));
}

void LedgerManager::GetPage(convert::ExtendedStringView page_id,
                            CreateIfNotFound create_if_not_found,
                            mojo::InterfaceRequest<Page> page_request,
                            std::function<void(Status)> callback) {
  // If we have the page manager ready, just ask for a new page impl.
  auto it = page_managers_.find(page_id);
  if (it != page_managers_.end()) {
    it->second->BindPage(std::move(page_request), std::move(callback));
    return;
  }

  PageManagerContainer* container = AddPageManagerContainer(page_id);
  container->BindPage(std::move(page_request), std::move(callback));

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
