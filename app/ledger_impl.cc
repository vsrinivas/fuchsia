// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/app/ledger_impl.h"

#include <memory>
#include <string>

#include "apps/ledger/api/ledger.mojom.h"
#include "apps/ledger/app/constants.h"
#include "apps/ledger/app/page_impl.h"
#include "apps/ledger/convert/convert.h"
#include "apps/ledger/glue/crypto/rand.h"
#include "apps/ledger/storage/public/page_storage.h"
#include "lib/ftl/files/path.h"
#include "lib/ftl/logging.h"
#include "mojo/public/cpp/bindings/strong_binding.h"

namespace ledger {

namespace {

storage::PageId RandomId() {
  std::string result;
  result.resize(kPageIdSize);
  glue::RandBytes(&result[0], kPageIdSize);
  return result;
}

}  // namespace

LedgerImpl::LedgerImpl(mojo::InterfaceRequest<Ledger> request,
                       std::unique_ptr<storage::LedgerStorage> storage)
    : binding_(this, std::move(request)), storage_(std::move(storage)) {}

LedgerImpl::~LedgerImpl() {}

// GetRootPage() => (Status status, Page? page);
void LedgerImpl::GetRootPage(const GetRootPageCallback& callback) {
  const storage::PageId page_id = storage::PageId(kRootPageId, kPageIdSize);

  // If we have the page manager ready, just ask for a new page impl.
  auto it = page_managers_.find(page_id);
  if (it != page_managers_.end()) {
    callback.Run(Status::OK, it->second->NewPageImpl());
    return;
  }

  storage_->GetPageStorage(
      page_id, [this, callback,
                &page_id](std::unique_ptr<storage::PageStorage> page_storage) {
        if (!page_storage) {
          storage::Status s =
              storage_->CreatePageStorage(page_id, &page_storage);
          if (s != storage::Status::OK) {
            callback.Run(Status::INTERNAL_ERROR, nullptr);
            return;
          }
        }
        callback.Run(
            Status::OK,
            AddPageManager(page_id, std::move(page_storage)).NewPageImpl());
      });
}

// GetPage(array<uint8> id) => (Status status, Page? page);
void LedgerImpl::GetPage(mojo::Array<uint8_t> id,
                         const GetPageCallback& callback) {
  const ftl::StringView page_id(reinterpret_cast<const char*>(id.data()),
                                id.size());

  // If we have the page manager ready, just ask for a new page impl.
  auto it = page_managers_.find(page_id);
  if (it != page_managers_.end()) {
    callback.Run(Status::OK, it->second->NewPageImpl());
    return;
  }

  // Need to create the page manager.
  storage_->GetPageStorage(
      convert::ToString(id),
      [this, callback,
       &page_id](std::unique_ptr<storage::PageStorage> page_storage) {
        if (!page_storage) {
          callback.Run(Status::PAGE_NOT_FOUND, nullptr);
          return;
        }
        callback.Run(Status::OK,
                     AddPageManager(page_id.ToString(), std::move(page_storage))
                         .NewPageImpl());
      });
}

// NewPage() => (Status status, Page? page);
void LedgerImpl::NewPage(const NewPageCallback& callback) {
  const storage::PageId page_id = RandomId();
  std::unique_ptr<storage::PageStorage> page_storage;
  storage::Status s = storage_->CreatePageStorage(page_id, &page_storage);
  if (s != storage::Status::OK) {
    callback.Run(Status::INTERNAL_ERROR, nullptr);
    return;
  }

  callback.Run(Status::OK,
               AddPageManager(page_id, std::move(page_storage)).NewPageImpl());
}

// DeletePage(array<uint8> id) => (Status status);
void LedgerImpl::DeletePage(mojo::Array<uint8_t> id,
                            const DeletePageCallback& callback) {
  const ftl::StringView page_id(reinterpret_cast<const char*>(id.data()),
                                id.size());
  auto it = page_managers_.find(page_id);
  if (it != page_managers_.end()) {
    page_managers_.erase(it);
  }

  if (storage_->DeletePageStorage(page_id.ToString())) {
    callback.Run(Status::OK);
  } else {
    callback.Run(Status::PAGE_NOT_FOUND);
  }
}

// SetConflictResolverFactory(ConflictResolverFactory? factory)
//     => (Status status);
void LedgerImpl::SetConflictResolverFactory(
    mojo::InterfaceHandle<ConflictResolverFactory> factory,
    const SetConflictResolverFactoryCallback& callback) {
  FTL_LOG(ERROR) << "LedgerImpl::SetConflictResolverFactory not implemented.";
  callback.Run(Status::UNKNOWN_ERROR);
}

PageManager& LedgerImpl::AddPageManager(
    const storage::PageId& page_id,
    std::unique_ptr<storage::PageStorage> page_storage) {
  auto ret = page_managers_.insert(std::make_pair(
      page_id,
      std::make_unique<PageManager>(std::move(page_storage), [this, page_id] {
        page_managers_.erase(page_id);
      })));
  FTL_DCHECK(ret.second);
  return *ret.first->second;
}

}  // namespace ledger
