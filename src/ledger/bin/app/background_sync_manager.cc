// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/ledger/bin/app/background_sync_manager.h"

#include <lib/async/cpp/task.h>
#include <lib/callback/waiter.h>

// #include <algorithm>

namespace ledger {
namespace {

// TODO(https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=35727): Change the limit from zero once
// page sync state is available.

// A maximum number of pages that should be opened at once.
inline constexpr size_t kOpenPagesLimit = 0;

// Returns the list of the |free_slots| pages (or less, if the initial size of the given list is
// smaller) with the earliest closure timestamps.
std::vector<PageInfo> SelectSyncCandidates(
    std::unique_ptr<storage::Iterator<const PageInfo>> pages_it, size_t free_slots) {
  std::vector<PageInfo> pages;
  while (pages_it->Valid()) {
    zx::time_utc timestamp = (*pages_it)->timestamp;
    if (timestamp != PageInfo::kOpenedPageTimestamp) {
      pages.push_back(**pages_it);
    }
    pages_it->Next();
  }
  if (free_slots < pages.size()) {
    std::nth_element(pages.begin(), pages.begin() + free_slots, pages.end(),
                     [](const PageInfo& info1, const PageInfo& info2) {
                       return std::tie(info1.timestamp, info1.ledger_name, info1.page_id) <
                              std::tie(info2.timestamp, info2.ledger_name, info2.page_id);
                     });
    pages.erase(pages.begin() + std::min(free_slots, pages.size()), pages.end());
  }
  return pages;
}

}  // namespace

BackgroundSyncManager::BackgroundSyncManager(Environment* environment, PageUsageDb* db)
    : BackgroundSyncManager(environment, db, kOpenPagesLimit) {}

BackgroundSyncManager::BackgroundSyncManager(Environment* environment, PageUsageDb* db,
                                             size_t open_pages_limit)
    : environment_(environment),
      db_(db),
      coroutine_manager_(environment_->coroutine_service()),
      open_pages_limit_(open_pages_limit),
      weak_factory_(this) {}

void BackgroundSyncManager::SetDelegate(Delegate* delegate) {
  FXL_DCHECK(delegate);
  FXL_DCHECK(!sync_delegate_);
  sync_delegate_ = delegate;
}

void BackgroundSyncManager::OnExternallyUsed(fxl::StringView ledger_name,
                                             storage::PageIdView page_id) {
  pages_connection_count_[{ledger_name.ToString(), page_id.ToString()}]++;
}

void BackgroundSyncManager::OnExternallyUnused(fxl::StringView ledger_name,
                                               storage::PageIdView page_id) {
  auto it = pages_connection_count_.find({ledger_name.ToString(), page_id.ToString()});
  FXL_DCHECK(it != pages_connection_count_.end());
  FXL_DCHECK(it->second > 0);
  it->second--;
  HandlePageIfUnused(it);
}

void BackgroundSyncManager::OnInternallyUsed(fxl::StringView ledger_name,
                                             storage::PageIdView page_id) {
  // Behavior is the same of external and internal connections.
  OnExternallyUsed(ledger_name, page_id);
}

void BackgroundSyncManager::OnInternallyUnused(fxl::StringView ledger_name,
                                               storage::PageIdView page_id) {
  // Behavior is the same of external and internal connections.
  OnExternallyUnused(ledger_name, page_id);
}

bool BackgroundSyncManager::IsEmpty() { return pending_operations_ == 0; }

void BackgroundSyncManager::HandlePageIfUnused(
    std::map<std::pair<std::string, storage::PageId>, int32_t>::iterator it) {
  if (it->second > 0) {
    return;
  }
  pages_connection_count_.erase(it);
  TrySync();
}

void BackgroundSyncManager::TrySync() {
  FXL_DCHECK(sync_delegate_);
  coroutine_manager_.StartCoroutine([this](coroutine::CoroutineHandler* handler) {
    // Ensure |this| is not destructed until the coroutine has completed.
    ExpiringToken token = NewExpiringToken();
    std::unique_ptr<storage::Iterator<const PageInfo>> pages_it;
    Status status = db_->GetPages(handler, &pages_it);
    if (status != Status::OK) {
      return;
    }
    size_t open_pages = pages_connection_count_.size();
    if (open_pages < open_pages_limit_) {
      std::vector<PageInfo> sync_candidates =
          SelectSyncCandidates(std::move(pages_it), open_pages_limit_ - open_pages);
      for (const auto& page : sync_candidates) {
        sync_delegate_->TrySyncClosedPage(page.ledger_name, page.page_id);
      }
    }
  });
}

ExpiringToken BackgroundSyncManager::NewExpiringToken() {
  ++pending_operations_;
  return ExpiringToken(callback::MakeScoped(weak_factory_.GetWeakPtr(), [this] {
    --pending_operations_;
    // We need to post a task here: Tokens expire while a coroutine is being executed, and if
    // |on_empty_callback_| is executed directly, it might end up deleting the backgroundSyncManager
    // object, which will delete the |coroutine_manager_|.
    async::PostTask(environment_->dispatcher(),
                    callback::MakeScoped(weak_factory_.GetWeakPtr(), [this] {
                      if (on_empty_callback_ && pending_operations_ == 0) {
                        on_empty_callback_();
                      }
                    }));
  }));
}

}  // namespace ledger
