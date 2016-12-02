// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/ledger_impl.h"

#include <memory>
#include <string>
#include <utility>

#include "apps/ledger/services/ledger.fidl.h"
#include "apps/ledger/src/app/constants.h"
#include "apps/ledger/src/app/page_impl.h"
#include "apps/ledger/src/callback/trace_callback.h"
#include "apps/tracing/lib/trace/event.h"
#include "lib/ftl/logging.h"

namespace ledger {

LedgerImpl::LedgerImpl(Delegate* delegate) : delegate_(delegate) {}

LedgerImpl::~LedgerImpl() {}

// GetRootPage(Page& page) => (Status status);
void LedgerImpl::GetRootPage(fidl::InterfaceRequest<Page> page_request,
                             const GetRootPageCallback& callback) {
  delegate_->GetPage(
      kRootPageId, Delegate::CreateIfNotFound::YES, std::move(page_request),
      callback::TraceCallback(std::move(callback), "ledger", "get_root_page"));
  ;
}

// GetPage(array<uint8> id, Page& page) => (Status status);
void LedgerImpl::GetPage(fidl::Array<uint8_t> id,
                         fidl::InterfaceRequest<Page> page_request,
                         const GetPageCallback& callback) {
  // TODO(etiennej): Switch back to Delegate::CreateIfNotFound::NO once LE-87 is
  // done.
  delegate_->GetPage(
      id, Delegate::CreateIfNotFound::YES, std::move(page_request),
      callback::TraceCallback(std::move(callback), "ledger", "get_page"));
}

// NewPage(Page& page) => (Status status);
void LedgerImpl::NewPage(fidl::InterfaceRequest<Page> page_request,
                         const NewPageCallback& callback) {
  delegate_->CreatePage(
      std::move(page_request),
      callback::TraceCallback(std::move(callback), "ledger", "new_page"));
}

// DeletePage(array<uint8> id) => (Status status);
void LedgerImpl::DeletePage(fidl::Array<uint8_t> id,
                            const DeletePageCallback& callback) {
  TRACE_DURATION0("ledger", "delete_page");

  callback(delegate_->DeletePage(id));
}

// SetConflictResolverFactory(ConflictResolverFactory? factory)
//     => (Status status);
void LedgerImpl::SetConflictResolverFactory(
    fidl::InterfaceHandle<ConflictResolverFactory> factory,
    const SetConflictResolverFactoryCallback& callback) {
  TRACE_DURATION0("ledger", "set_conflict_resolver_factory");

  FTL_NOTIMPLEMENTED();
  callback(Status::UNKNOWN_ERROR);
}

}  // namespace ledger
