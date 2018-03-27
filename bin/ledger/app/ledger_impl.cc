// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/ledger_impl.h"

#include <memory>
#include <string>
#include <utility>

#include <trace/event.h>

#include <fuchsia/cpp/ledger.h>
#include "garnet/lib/callback/trace_callback.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/random/rand.h"
#include "peridot/bin/ledger/app/constants.h"
#include "peridot/bin/ledger/app/page_impl.h"

namespace ledger {

namespace {

void GenerateRandomId(fidl::VectorPtr<uint8_t>* id) {
  id->resize(kPageIdSize);
  fxl::RandBytes((*id)->data(), kPageIdSize);
}

void GenerateRandomId(::fidl::Array<uint8_t, kPageIdSize>* id) {
  fxl::RandBytes(id.mutable_data(), kPageIdSize);
}

}  // namespace

LedgerImpl::LedgerImpl(Delegate* delegate) : delegate_(delegate) {}

LedgerImpl::~LedgerImpl() {}

// GetRootPage(Page& page) => (Status status);
void LedgerImpl::GetRootPage(fidl::InterfaceRequest<Page> page_request,
                             GetRootPageCallback callback) {
  delegate_->GetPage(
      kRootPageId, std::move(page_request),
      TRACE_CALLBACK(callback, "ledger", "ledger_get_root_page"));
}

// GetPage(array<uint8, 16>? id, Page& page) => (Status status);
void LedgerImpl::GetPage(PageIdPtr id,
                         fidl::InterfaceRequest<Page> page_request,
                         GetPageCallback callback) {
  if (!id) {
    id = fidl::MakeOptional(PageId());
    GenerateRandomId(&id->id);
  }
  delegate_->GetPage(id->id, std::move(page_request),
                     TRACE_CALLBACK(callback, "ledger", "ledger_get_page"));
}

// DeletePage(array<uint8> id) => (Status status);
void LedgerImpl::DeletePage(PageId id, DeletePageCallback callback) {
  TRACE_DURATION("ledger", "ledger_delete_page");

  callback(delegate_->DeletePage(id));
}

// SetConflictResolverFactory(ConflictResolverFactory? factory)
//     => (Status status);
void LedgerImpl::SetConflictResolverFactory(
    fidl::InterfaceHandle<ConflictResolverFactory> factory,
    SetConflictResolverFactoryCallback callback) {
  TRACE_DURATION("ledger", "ledger_set_conflict_resolver_factory");

  delegate_->SetConflictResolverFactory(std::move(factory));
  callback(Status::OK);
}

}  // namespace ledger
