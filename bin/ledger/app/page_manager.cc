// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/page_manager.h"

#include <algorithm>

#include "apps/ledger/services/public/ledger.fidl.h"
#include "lib/ftl/logging.h"

namespace ledger {

PageManager::PageManager(
    Environment* environment,
    std::unique_ptr<storage::PageStorage> page_storage,
    std::unique_ptr<cloud_sync::PageSyncContext> page_sync_context,
    std::unique_ptr<MergeResolver> merge_resolver,
    ftl::TimeDelta sync_timeout)
    : environment_(environment),
      page_storage_(std::move(page_storage)),
      page_sync_context_(std::move(page_sync_context)),
      merge_resolver_(std::move(merge_resolver)),
      sync_timeout_(sync_timeout),
      weak_factory_(this) {
  pages_.set_on_empty([this] { CheckEmpty(); });
  snapshots_.set_on_empty([this] { CheckEmpty(); });
  if (page_sync_context) {
    page_sync_context_->page_sync->SetSyncWatcher(&watchers_);
    page_sync_context_->page_sync->SetOnIdle([this] { CheckEmpty(); });
  }

  if (page_sync_context_) {
    page_sync_context_->page_sync->SetOnBacklogDownloaded(
        [this] { OnSyncBacklogDownloaded(); });
    page_sync_context_->page_sync->Start();
    environment_->main_runner()->PostDelayedTask(
        [weak_this = weak_factory_.GetWeakPtr()]() {
          if (weak_this && !weak_this->sync_backlog_downloaded_) {
            FTL_LOG(INFO) << "Initial sync will continue in background, "
                          << "in the meantime binding to local page data "
                          << "(might be stale or empty).";
            weak_this->OnSyncBacklogDownloaded();
          }
        },
        sync_timeout_);
  } else {
    sync_backlog_downloaded_ = true;
  }
  merge_resolver_->set_on_empty([this] { CheckEmpty(); });
  merge_resolver_->SetPageManager(this);
}

PageManager::~PageManager() {}

void PageManager::BindPage(fidl::InterfaceRequest<Page> page_request) {
  if (sync_backlog_downloaded_) {
    pages_.emplace(environment_->coroutine_service(), this, page_storage_.get(),
                   std::move(page_request), &watchers_);
  } else {
    page_requests_.push_back(std::move(page_request));
  }
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
    FTL_LOG(INFO) << "Initial sync in background finished. "
                  << "Clients will receive a change notification.";
  }
  sync_backlog_downloaded_ = true;
  for (auto& request : page_requests_) {
    BindPage(std::move(request));
  }
  page_requests_.clear();
}

}  // namespace ledger
