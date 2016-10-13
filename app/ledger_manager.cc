// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/app/ledger_manager.h"

#include "apps/ledger/app/constants.h"
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

LedgerManager::LedgerManager(std::unique_ptr<storage::LedgerStorage> storage)
    : storage_(std::move(storage)), ledger_impl_(this) {}

LedgerManager::~LedgerManager() {}

LedgerPtr LedgerManager::GetLedgerPtr() {
  LedgerPtr ledger;
  bindings_.AddBinding(&ledger_impl_, GetProxy(&ledger));
  return ledger;
}

void LedgerManager::CreatePage(std::function<void(Status, PagePtr)> callback) {
  const storage::PageId page_id = RandomId();
  std::unique_ptr<storage::PageStorage> page_storage;

  storage::Status status = storage_->CreatePageStorage(page_id, &page_storage);
  if (status != storage::Status::OK) {
    callback(Status::INTERNAL_ERROR, nullptr);
    return;
  }

  callback(Status::OK,
           AddPageManagerAndGetPagePtr(page_id, std::move(page_storage)));
}

void LedgerManager::GetPage(ftl::StringView page_id,
                            CreateIfNotFound create_if_not_found,
                            std::function<void(Status, PagePtr)> callback) {
  // If we have the page manager ready, just ask for a new page impl.
  auto it = page_managers_.find(page_id);
  if (it != page_managers_.end()) {
    callback(Status::OK, it->second->GetPagePtr());
    return;
  }

  const std::string page_id_str = page_id.ToString();
  storage_->GetPageStorage(
      page_id_str, [this, create_if_not_found, page_id_str, callback](
                       std::unique_ptr<storage::PageStorage> page_storage) {
        if (!page_storage) {
          if (create_if_not_found == CreateIfNotFound::NO) {
            callback(Status::PAGE_NOT_FOUND, nullptr);
            return;
          }
          storage::Status status =
              storage_->CreatePageStorage(page_id_str, &page_storage);
          if (status != storage::Status::OK) {
            callback(Status::INTERNAL_ERROR, nullptr);
            return;
          }
        }
        callback(Status::OK, AddPageManagerAndGetPagePtr(
                                 page_id_str, std::move(page_storage)));
      });
}

Status LedgerManager::DeletePage(ftl::StringView page_id) {
  auto it = page_managers_.find(page_id);
  if (it != page_managers_.end()) {
    page_managers_.erase(it);
  }

  if (storage_->DeletePageStorage(page_id.ToString())) {
    return Status::OK;
  } else {
    return Status::PAGE_NOT_FOUND;
  }
}

PagePtr LedgerManager::AddPageManagerAndGetPagePtr(
    storage::PageIdView page_id,
    std::unique_ptr<storage::PageStorage> page_storage) {
  auto ret = page_managers_.insert(std::make_pair(
      page_id.ToString(),
      std::make_unique<PageManager>(std::move(page_storage),
                                    [ this, page_id = page_id.ToString() ] {
                                      page_managers_.erase(page_id);
                                    })));
  FTL_DCHECK(ret.second);
  return ret.first->second->GetPagePtr();
}

}  // namespace ledger
