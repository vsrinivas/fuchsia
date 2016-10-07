// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/app/ledger_impl.h"

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
  storage_->GetPageStorage(
      storage::PageId(kRootPageId, kPageIdSize),
      [this, callback](std::unique_ptr<storage::PageStorage> page_storage) {
        if (!page_storage) {
          page_storage = storage_->CreatePageStorage(
              storage::PageId(kRootPageId, kPageIdSize));
          if (!page_storage) {
            callback.Run(Status::PAGE_NOT_FOUND, nullptr);
            return;
          }
        }
        PagePtr page;
        new PageImpl(GetProxy(&page), std::move(page_storage));
        callback.Run(Status::OK, std::move(page));
      });
}

// GetPage(array<uint8> id) => (Status status, Page? page);
void LedgerImpl::GetPage(mojo::Array<uint8_t> id,
                         const GetPageCallback& callback) {
  storage_->GetPageStorage(
      convert::ToString(id),
      [this, callback](std::unique_ptr<storage::PageStorage> page_storage) {
        if (!page_storage) {
          callback.Run(Status::PAGE_NOT_FOUND, nullptr);
          return;
        }
        PagePtr page;
        new PageImpl(GetProxy(&page), std::move(page_storage));
        callback.Run(Status::OK, std::move(page));
      });
}

// NewPage() => (Status status, Page? page);
void LedgerImpl::NewPage(const NewPageCallback& callback) {
  storage::PageId id = RandomId();
  PagePtr page;
  new PageImpl(GetProxy(&page), storage_->CreatePageStorage(id));
  callback.Run(Status::OK, std::move(page));
}

// DeletePage(array<uint8> id) => (Status status);
void LedgerImpl::DeletePage(mojo::Array<uint8_t> id,
                            const DeletePageCallback& callback) {
  if (storage_->DeletePageStorage(convert::ToString(id))) {
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

}  // namespace ledger
