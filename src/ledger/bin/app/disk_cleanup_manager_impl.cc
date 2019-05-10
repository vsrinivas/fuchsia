// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/disk_cleanup_manager_impl.h"

namespace ledger {

DiskCleanupManagerImpl::DiskCleanupManagerImpl(Environment* environment,
                                               storage::DbFactory* db_factory,
                                               DetachedPath db_path)
    : page_eviction_manager_(environment, db_factory, std::move(db_path)),
      policy_(NewLeastRecentyUsedPolicy(environment->coroutine_service(),
                                        &page_eviction_manager_)) {}

DiskCleanupManagerImpl::~DiskCleanupManagerImpl() {}

void DiskCleanupManagerImpl::Init() { page_eviction_manager_.Init(); }

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
  page_eviction_manager_.TryEvictPages(policy_.get(), std::move(callback));
}

void DiskCleanupManagerImpl::OnPageOpened(fxl::StringView ledger_name,
                                          storage::PageIdView page_id) {
  page_eviction_manager_.MarkPageOpened(ledger_name, page_id);
}

void DiskCleanupManagerImpl::OnPageClosed(fxl::StringView ledger_name,
                                          storage::PageIdView page_id) {
  page_eviction_manager_.MarkPageClosed(ledger_name, page_id);
}

void DiskCleanupManagerImpl::OnPageUnused(fxl::StringView ledger_name,
                                          storage::PageIdView page_id) {
  page_eviction_manager_.TryEvictPage(
      ledger_name, page_id, PageEvictionCondition::IF_EMPTY,
      [ledger_name = ledger_name.ToString(), page_id = page_id.ToString()](
          Status status, PageWasEvicted) {
        FXL_DCHECK(status != Status::INTERRUPTED);
        if (status != Status::OK) {
          FXL_LOG(ERROR) << "Failed to check if page is empty and/or evict it. "
                            "Status: "
                         << fidl::ToUnderlying(status)
                         << ". Ledger name: " << ledger_name
                         << ". Page ID: " << convert::ToHex(page_id);
        }
      });
}

}  // namespace ledger
