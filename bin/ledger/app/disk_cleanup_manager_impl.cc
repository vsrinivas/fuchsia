// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/disk_cleanup_manager_impl.h"

namespace ledger {

DiskCleanupManagerImpl::DiskCleanupManagerImpl(
    async_dispatcher_t* dispatcher,
    coroutine::CoroutineService* coroutine_service,
    ledger::DetachedPath db_path)
    : page_eviction_manager_(dispatcher, coroutine_service,
                             std::move(db_path)) {}

DiskCleanupManagerImpl::~DiskCleanupManagerImpl() {}

Status DiskCleanupManagerImpl::Init() { return page_eviction_manager_.Init(); }

void DiskCleanupManagerImpl::SetPageEvictionDelegate(
    PageEvictionManager::Delegate* delegate) {
  page_eviction_manager_.SetDelegate(delegate);
}

void DiskCleanupManagerImpl::set_on_empty(fit::closure on_empty_callback) {
  page_eviction_manager_.set_on_empty(std::move(on_empty_callback));
}

bool DiskCleanupManagerImpl::IsEmpty() {
  return page_eviction_manager_.IsEmpty();
}

void DiskCleanupManagerImpl::TryCleanUp(fit::function<void(Status)> callback) {
  page_eviction_manager_.TryEvictPages(std::move(callback));
}

void DiskCleanupManagerImpl::OnPageOpened(fxl::StringView ledger_name,
                                          storage::PageIdView page_id) {
  page_eviction_manager_.OnPageOpened(ledger_name, page_id);
}

void DiskCleanupManagerImpl::OnPageClosed(fxl::StringView ledger_name,
                                          storage::PageIdView page_id) {
  page_eviction_manager_.OnPageClosed(ledger_name, page_id);
}

}  // namespace ledger
