// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/page_manager.h"

#include <algorithm>

#include "lib/fxl/logging.h"
#include "lib/ledger/fidl/ledger.fidl.h"

namespace ledger {

PageManager::PageManager(
    Environment* environment,
    std::unique_ptr<storage::PageStorage> page_storage,
    std::unique_ptr<cloud_sync::PageSyncContext> page_sync_context,
    std::unique_ptr<MergeResolver> merge_resolver,
    PageManager::PageStorageState state,
    fxl::TimeDelta sync_timeout)
    : environment_(environment),
      page_storage_(std::move(page_storage)),
      page_sync_context_(std::move(page_sync_context)),
      merge_resolver_(std::move(merge_resolver)),
      sync_timeout_(sync_timeout),
      task_runner_(environment->main_runner()) {
  pages_.set_on_empty([this] { CheckEmpty(); });
  snapshots_.set_on_empty([this] { CheckEmpty(); });

  if (page_sync_context_) {
    page_sync_context_->page_sync->SetSyncWatcher(&watchers_);
    page_sync_context_->page_sync->SetOnIdle([this] { CheckEmpty(); });
    page_sync_context_->page_sync->SetOnBacklogDownloaded(
        [this] { OnSyncBacklogDownloaded(); });
    page_sync_context_->page_sync->Start();
    if (state == PageManager::PageStorageState::NEW) {
      // The page storage was created locally. We wait a bit in order to get the
      // initial state from the network before accepting requests.
      task_runner_.PostDelayedTask(
          [this] {
            if (!sync_backlog_downloaded_) {
              FXL_LOG(INFO) << "Initial sync will continue in background, "
                            << "in the meantime binding to local page data "
                            << "(might be stale or empty).";
              OnSyncBacklogDownloaded();
            }
          },
          sync_timeout_);
    } else {
      sync_backlog_downloaded_ = true;
    }
  } else {
    sync_backlog_downloaded_ = true;
  }
  merge_resolver_->set_on_empty([this] { CheckEmpty(); });
  merge_resolver_->SetPageManager(this);
}

PageManager::~PageManager() {
  for (const auto& request : page_requests_) {
    request.second(Status::INTERNAL_ERROR);
  }
  page_requests_.clear();
}

void PageManager::BindPage(fidl::InterfaceRequest<Page> page_request,
                           std::function<void(Status)> on_done) {
  if (sync_backlog_downloaded_) {
    pages_
        .emplace(environment_->coroutine_service(), this, page_storage_.get(),
                 std::move(page_request), &watchers_)
        .Init(std::move(on_done));
    return;
  }
  page_requests_.emplace_back(std::move(page_request), std::move(on_done));
}

void PageManager::BindPageSnapshot(
    std::unique_ptr<const storage::Commit> commit,
    fidl::InterfaceRequest<PageSnapshot> snapshot_request,
    std::string key_prefix) {
  snapshots_.emplace(std::move(snapshot_request), page_storage_.get(),
                     std::move(commit), std::move(key_prefix));
}

void PageManager::CheckEmpty() {
  if (on_empty_callback_ && pages_.empty() && snapshots_.empty() &&
      page_requests_.empty() && merge_resolver_->IsEmpty() &&
      (!page_sync_context_ || page_sync_context_->page_sync->IsIdle())) {
    on_empty_callback_();
  }
}

void PageManager::OnSyncBacklogDownloaded() {
  if (sync_backlog_downloaded_) {
    FXL_LOG(INFO) << "Initial sync in background finished. "
                  << "Clients will receive a change notification.";
  }
  sync_backlog_downloaded_ = true;
  for (auto& page_request : page_requests_) {
    BindPage(std::move(page_request.first), std::move(page_request.second));
  }
  page_requests_.clear();
}

}  // namespace ledger
