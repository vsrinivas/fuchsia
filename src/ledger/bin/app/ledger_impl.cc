// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/ledger_impl.h"

#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/trace/event.h>
#include <zircon/syscalls.h>

#include <memory>
#include <string>
#include <utility>

#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/page_impl.h"
#include "src/ledger/bin/app/page_utils.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/lib/callback/trace_callback.h"

namespace ledger {

LedgerImpl::LedgerImpl(Environment* environment, Delegate* delegate)
    : environment_(environment), delegate_(delegate) {}

LedgerImpl::~LedgerImpl() = default;

void LedgerImpl::GetRootPage(fidl::InterfaceRequest<Page> page_request,
                             fit::function<void(Status)> callback) {
  delegate_->GetPage(kRootPageId, Delegate::PageState::NAMED, std::move(page_request),
                     TRACE_CALLBACK(std::move(callback), "ledger", "ledger_get_root_page"));
}

void LedgerImpl::GetPage(PageIdPtr id, fidl::InterfaceRequest<Page> page_request,
                         fit::function<void(Status)> callback) {
  Delegate::PageState page_state = Delegate::PageState::NAMED;
  if (!id) {
    id = fidl::MakeOptional(PageId());
    environment_->random()->Draw(&id->id);
    page_state = Delegate::PageState::NEW;
  }
  delegate_->GetPage(id->id, page_state, std::move(page_request),
                     TRACE_CALLBACK(std::move(callback), "ledger", "ledger_get_page"));
}

void LedgerImpl::SetConflictResolverFactory(fidl::InterfaceHandle<ConflictResolverFactory> factory,
                                            fit::function<void(Status)> callback) {
  TRACE_DURATION("ledger", "ledger_set_conflict_resolver_factory");

  delegate_->SetConflictResolverFactory(std::move(factory));
  callback(Status::OK);
}

}  // namespace ledger
