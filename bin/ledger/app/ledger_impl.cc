// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/ledger_impl.h"

#include <memory>
#include <string>
#include <utility>

#include <lib/callback/trace_callback.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fxl/logging.h>
#include <trace/event.h>
#include <zircon/syscalls.h>

#include "peridot/bin/ledger/app/constants.h"
#include "peridot/bin/ledger/app/page_impl.h"
#include "peridot/bin/ledger/fidl/include/types.h"

namespace ledger {

namespace {

void GenerateRandomId(::fidl::Array<uint8_t, kPageIdSize>* id) {
  zx_cprng_draw(id->mutable_data(), kPageIdSize);
}

}  // namespace

LedgerImpl::LedgerImpl(Delegate* delegate) : delegate_(delegate) {}

LedgerImpl::~LedgerImpl() {}

void LedgerImpl::GetRootPage(fidl::InterfaceRequest<Page> page_request,
                             GetRootPageCallback callback) {
  delegate_->GetPage(
      kRootPageId, Delegate::PageState::NAMED, std::move(page_request),
      TRACE_CALLBACK(std::move(callback), "ledger", "ledger_get_root_page"));
}

void LedgerImpl::GetPage(PageIdPtr id,
                         fidl::InterfaceRequest<Page> page_request,
                         GetPageCallback callback) {
  Delegate::PageState page_state = Delegate::PageState::NAMED;
  if (!id) {
    id = fidl::MakeOptional(PageId());
    GenerateRandomId(&id->id);
    page_state = Delegate::PageState::NEW;
  }
  delegate_->GetPage(
      id->id, page_state, std::move(page_request),
      TRACE_CALLBACK(std::move(callback), "ledger", "ledger_get_page"));
}

void LedgerImpl::SetConflictResolverFactory(
    fidl::InterfaceHandle<ConflictResolverFactory> factory,
    SetConflictResolverFactoryCallback callback) {
  TRACE_DURATION("ledger", "ledger_set_conflict_resolver_factory");

  delegate_->SetConflictResolverFactory(std::move(factory));
  callback(Status::OK);
}

}  // namespace ledger
