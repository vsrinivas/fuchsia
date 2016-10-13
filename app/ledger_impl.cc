// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/app/ledger_impl.h"

#include <memory>
#include <string>
#include <utility>

#include "apps/ledger/api/ledger.mojom.h"
#include "apps/ledger/app/constants.h"
#include "apps/ledger/app/page_impl.h"
#include "lib/ftl/logging.h"

namespace ledger {

LedgerImpl::LedgerImpl(Delegate* delegate) : delegate_(delegate) {}

LedgerImpl::~LedgerImpl() {}

// GetRootPage() => (Status status, Page? page);
void LedgerImpl::GetRootPage(const GetRootPageCallback& callback) {
  delegate_->GetPage(kRootPageId, Delegate::CreateIfNotFound::YES,
                     [callback](Status status, PagePtr page) {
                       callback.Run(status, std::move(page));
                     });
}

// GetPage(array<uint8> id) => (Status status, Page? page);
void LedgerImpl::GetPage(mojo::Array<uint8_t> id,
                         const GetPageCallback& callback) {
  delegate_->GetPage(id, Delegate::CreateIfNotFound::NO,
                     [callback](Status status, PagePtr page) {
                       callback.Run(status, std::move(page));
                     });
}

// NewPage() => (Status status, Page? page);
void LedgerImpl::NewPage(const NewPageCallback& callback) {
  delegate_->CreatePage([callback](Status status, PagePtr page) {
    callback.Run(status, std::move(page));
  });
}

// DeletePage(array<uint8> id) => (Status status);
void LedgerImpl::DeletePage(mojo::Array<uint8_t> id,
                            const DeletePageCallback& callback) {
  callback.Run(delegate_->DeletePage(id));
}

// SetConflictResolverFactory(ConflictResolverFactory? factory)
//     => (Status status);
void LedgerImpl::SetConflictResolverFactory(
    mojo::InterfaceHandle<ConflictResolverFactory> factory,
    const SetConflictResolverFactoryCallback& callback) {
  FTL_NOTIMPLEMENTED();
  callback.Run(Status::UNKNOWN_ERROR);
}

}  // namespace ledger
