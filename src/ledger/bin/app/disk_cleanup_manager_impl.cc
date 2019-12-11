// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/disk_cleanup_manager_impl.h"

#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/logging/logging.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {

DiskCleanupManagerImpl::DiskCleanupManagerImpl(Environment* environment, PageUsageDb* db)
    : page_eviction_manager_(environment, db),
      policy_(
          NewLeastRecentyUsedPolicy(environment->coroutine_service(), &page_eviction_manager_)) {}

DiskCleanupManagerImpl::~DiskCleanupManagerImpl() = default;

void DiskCleanupManagerImpl::SetPageEvictionDelegate(PageEvictionManager::Delegate* delegate) {
  page_eviction_manager_.SetDelegate(delegate);
}

void DiskCleanupManagerImpl::SetOnDiscardable(fit::closure on_discardable) {
  page_eviction_manager_.SetOnDiscardable(std::move(on_discardable));
}

bool DiskCleanupManagerImpl::IsDiscardable() const {
  return page_eviction_manager_.IsDiscardable();
}

void DiskCleanupManagerImpl::TryCleanUp(fit::function<void(Status)> callback) {
  page_eviction_manager_.TryEvictPages(policy_.get(), std::move(callback));
}

void DiskCleanupManagerImpl::OnExternallyUsed(absl::string_view ledger_name,
                                              storage::PageIdView page_id) {
  PageState& page_state =
      pages_state_[{convert::ToString(ledger_name), convert::ToString(page_id)}];
  page_state.external_connections_count++;
  page_state.is_eviction_candidate = true;
  page_eviction_manager_.MarkPageOpened(ledger_name, page_id);
}

void DiskCleanupManagerImpl::OnExternallyUnused(absl::string_view ledger_name,
                                                storage::PageIdView page_id) {
  auto it = pages_state_.find({convert::ToString(ledger_name), convert::ToString(page_id)});
  LEDGER_DCHECK(it != pages_state_.end());
  LEDGER_DCHECK(it->second.external_connections_count > 0);
  it->second.external_connections_count--;
  HandlePageIfUnused(it, ledger_name, page_id);

  page_eviction_manager_.MarkPageClosed(ledger_name, page_id);
}

void DiskCleanupManagerImpl::OnInternallyUsed(absl::string_view ledger_name,
                                              storage::PageIdView page_id) {
  pages_state_[{convert::ToString(ledger_name), convert::ToString(page_id)}]
      .internal_connections_count++;
}

void DiskCleanupManagerImpl::OnInternallyUnused(absl::string_view ledger_name,
                                                storage::PageIdView page_id) {
  auto it = pages_state_.find({convert::ToString(ledger_name), convert::ToString(page_id)});
  LEDGER_DCHECK(it != pages_state_.end());
  LEDGER_DCHECK(it->second.internal_connections_count > 0);
  it->second.internal_connections_count--;
  HandlePageIfUnused(it, ledger_name, page_id);
}

void DiskCleanupManagerImpl::HandlePageIfUnused(
    std::map<std::pair<std::string, storage::PageId>, PageState>::iterator it,
    absl::string_view ledger_name, storage::PageIdView page_id) {
  PageState page_state = it->second;
  if (page_state.internal_connections_count > 0 || page_state.external_connections_count > 0) {
    return;
  }
  // The page is now closed, we can remove the entry.
  pages_state_.erase(it);
  if (page_state.is_eviction_candidate) {
    // An update to a Page might have occurred from an external connection only (internal ones do
    // not edit commits). If there was an external connetion while the page was open (internally or
    // externally), we might be able to evict the page if it is cleared.
    page_eviction_manager_.TryEvictPage(
        ledger_name, page_id, PageEvictionCondition::IF_EMPTY,
        [ledger_name = convert::ToString(ledger_name), page_id = convert::ToString(page_id)](
            Status status, PageWasEvicted) {
          LEDGER_DCHECK(status != Status::INTERRUPTED);
          if (status != Status::OK) {
            LEDGER_LOG(ERROR) << "Failed to check if page is empty and/or evict it. Status: "
                              << fidl::ToUnderlying(status) << ". Ledger name: " << ledger_name
                              << ". Page ID: " << convert::ToHex(page_id);
          }
        });
  }
}

}  // namespace ledger
