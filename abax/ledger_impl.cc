// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/abax/ledger_impl.h"

#include <string.h>

#include "apps/ledger/abax/constants.h"
#include "apps/ledger/abax/page_connector.h"
#include "apps/ledger/abax/page_impl.h"
#include "apps/ledger/api/ledger.mojom.h"
#include "apps/ledger/glue/crypto/rand.h"
#include "lib/ftl/logging.h"
#include "mojo/public/cpp/bindings/strong_binding.h"

namespace ledger {

// The zero-initialized root id.
const mojo::Array<uint8_t> kRootPageId = mojo::Array<uint8_t>::New(kPageIdSize);

namespace {

mojo::Array<uint8_t> RandomId() {
  FTL_DCHECK(kPageIdSize % 8 == 0);
  mojo::Array<uint8_t> id = mojo::Array<uint8_t>::New(kPageIdSize);
  for (size_t i = 0; i < kPageIdSize / 8; ++i) {
    uint64_t random_value = glue::RandUint64();
    for (size_t j = 0; j < 8; ++j) {
      id[8 * i + j] = random_value & 0xFF;
      random_value = random_value >> 8;
    }
  }
  return id;
}

}  // namespace

size_t LedgerImpl::PageIdHash::operator()(
    const mojo::Array<uint8_t>& id) const {
  FTL_DCHECK(id.size() == kPageIdSize);
  size_t result = 5381;
  for (uint8_t b : id.storage())
    result = ((result << 5) + result) ^ b;
  return result;
}

size_t LedgerImpl::PageIdEquals::operator()(
    const mojo::Array<uint8_t>& id1,
    const mojo::Array<uint8_t>& id2) const {
  FTL_DCHECK(id1.size() == kPageIdSize && id2.size() == kPageIdSize);
  return id1.Equals(id2);
}

LedgerImpl::LedgerImpl(mojo::InterfaceRequest<Ledger> request)
    : binding_(this, std::move(request)) {}

LedgerImpl::~LedgerImpl() {}

Status LedgerImpl::Init() {
  return Status::OK;
}

// GetRootPage() => (Status status, Page? page);
void LedgerImpl::GetRootPage(const GetRootPageCallback& callback) {
  PageImpl* pageImpl = GetPageImpl(kRootPageId);
  if (pageImpl == nullptr) {
    // Create a new PageImpl and cache it.
    pageImpl = CachePageImpl(kRootPageId, NewPageImpl(kRootPageId));
  }
  // Initialize page if necessary.
  if (!pageImpl->Exists()) {
    pageImpl->Initialize();
  }
  PagePtr page;
  pageImpl->AddConnector(GetProxy(&page));
  callback.Run(Status::OK, std::move(page));
}

// GetPage(array<uint8> id) => (Status status, Page? page);
void LedgerImpl::GetPage(mojo::Array<uint8_t> id,
                         const GetPageCallback& callback) {
  PageImpl* pageImpl = GetPageImpl(id);
  if (pageImpl == nullptr) {
    // The PageImpl object needs to be created to check if the page is created.
    // Avoid however putting it in the cache if it doesn't exist.
    std::unique_ptr<PageImpl> uniquePage = NewPageImpl(id);
    if (!uniquePage->Exists()) {
      callback.Run(Status::PAGE_NOT_FOUND, nullptr);
      return;
    }
    pageImpl = CachePageImpl(id, std::move(uniquePage));
  }
  // Check if the Page exists.
  if (!pageImpl->Exists()) {
    callback.Run(Status::PAGE_NOT_FOUND, nullptr);
  } else {
    PagePtr page;
    pageImpl->AddConnector(GetProxy(&page));
    callback.Run(Status::OK, std::move(page));
  }
}

// NewPage() => (Status status, Page? page);
void LedgerImpl::NewPage(const NewPageCallback& callback) {
  mojo::Array<uint8_t> id = RandomId();
  PagePtr page;
  PageImpl* pageImpl = CachePageImpl(id, NewPageImpl(id));
  pageImpl->Initialize();
  pageImpl->AddConnector(GetProxy(&page));
  callback.Run(Status::OK, std::move(page));
}

// DeletePage(array<uint8> id) => (Status status);
void LedgerImpl::DeletePage(mojo::Array<uint8_t> id,
                            const DeletePageCallback& callback) {
  PageImpl* pageImpl = GetPageImpl(id);
  std::unique_ptr<PageImpl> uniquePage;
  if (pageImpl == nullptr) {
    uniquePage = NewPageImpl(id);
    pageImpl = uniquePage.get();
  }

  if (pageImpl->Exists()) {
    Status status = pageImpl->Delete();
    if (status == Status::OK) {
      page_map_.erase(id);
    }
    callback.Run(status);
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

PageImpl* LedgerImpl::GetPageImpl(const mojo::Array<uint8_t>& page_id) {
  auto it = page_map_.find(page_id);
  if (it != page_map_.end()) {
    return it->second.get();
  }
  return nullptr;
}

PageImpl* LedgerImpl::CachePageImpl(const mojo::Array<uint8_t>& page_id,
                                    std::unique_ptr<PageImpl> page) {
  PageImpl* result = page.get();
  page_map_[page_id.Clone()] = std::move(page);
  return result;
}

std::unique_ptr<PageImpl> LedgerImpl::NewPageImpl(
    const mojo::Array<uint8_t>& page_id) {
  return std::unique_ptr<PageImpl>(new PageImpl(page_id.Clone(), &db_, this));
}

void LedgerImpl::OnPageError(const mojo::Array<uint8_t>& id) {
  page_map_.erase(id);
}

}  // namespace ledger
