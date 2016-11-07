// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/ledger_impl.h"

#include <memory>
#include <string>
#include <utility>

#include "apps/ledger/api/ledger.mojom.h"
#include "apps/ledger/src/app/constants.h"
#include "apps/ledger/src/app/page_impl.h"
#include "lib/ftl/logging.h"

namespace ledger {

LedgerImpl::LedgerImpl(Delegate* delegate) : delegate_(delegate) {}

LedgerImpl::~LedgerImpl() {}

// GetRootPage(Page& page) => (Status status);
void LedgerImpl::GetRootPage(mojo::InterfaceRequest<Page> page_request,
                             const GetRootPageCallback& callback) {
  delegate_->GetPage(kRootPageId, Delegate::CreateIfNotFound::YES,
                     std::move(page_request),
                     [callback](Status status) { callback.Run(status); });
}

// GetPage(array<uint8> id, Page& page) => (Status status);
void LedgerImpl::GetPage(mojo::Array<uint8_t> id,
                         mojo::InterfaceRequest<Page> page_request,
                         const GetPageCallback& callback) {
  delegate_->GetPage(id, Delegate::CreateIfNotFound::NO,
                     std::move(page_request),
                     [callback](Status status) { callback.Run(status); });
}

// NewPage(Page& page) => (Status status);
void LedgerImpl::NewPage(mojo::InterfaceRequest<Page> page_request,
                         const NewPageCallback& callback) {
  delegate_->CreatePage(std::move(page_request),
                        [callback](Status status) { callback.Run(status); });
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
