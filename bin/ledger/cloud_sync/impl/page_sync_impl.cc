// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/cloud_sync/impl/page_sync_impl.h"

#include <algorithm>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "lib/fxl/functional/closure.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "peridot/bin/ledger/cloud_sync/impl/constants.h"
#include "peridot/bin/ledger/storage/public/types.h"

namespace cloud_sync {

PageSyncImpl::PageSyncImpl(async_t* async, storage::PageStorage* storage,
                           storage::PageSyncClient* sync_client,
                           encryption::EncryptionService* encryption_service,
                           cloud_provider::PageCloudPtr page_cloud,
                           std::unique_ptr<backoff::Backoff> download_backoff,
                           std::unique_ptr<backoff::Backoff> upload_backoff,
                           fxl::Closure on_error,
                           std::unique_ptr<SyncStateWatcher> ledger_watcher)
    : storage_(storage),
      sync_client_(sync_client),
      encryption_service_(encryption_service),
      page_cloud_(std::move(page_cloud)),
      on_error_(std::move(on_error)),
      log_prefix_("Page " + convert::ToHex(storage->GetId()) + " sync: "),
      ledger_watcher_(std::move(ledger_watcher)),
      task_runner_(async) {
  FXL_DCHECK(storage_);
  FXL_DCHECK(page_cloud_);
  // We need to initialize page_download_ after task_runner_, but task_runner_
  // must be the last field.
  page_download_ = std::make_unique<PageDownload>(
      &task_runner_, storage_, sync_client_, encryption_service_, &page_cloud_,
      this, std::move(download_backoff));
  page_upload_ = std::make_unique<PageUpload>(&task_runner_, storage_,
                                              encryption_service_, &page_cloud_,
                                              this, std::move(upload_backoff));
  page_cloud_.set_error_handler([] {
    FXL_LOG(ERROR) << "Page cloud disconnected unexpectedly.";
    // TODO(ppi): we should probably shut down page download and upload.
  });
}

PageSyncImpl::~PageSyncImpl() {
  if (on_delete_) {
    on_delete_();
  }
}

void PageSyncImpl::EnableUpload() {
  enable_upload_ = true;

  if (!started_) {
    // We will start upload when this object is started.
    return;
  }

  if (upload_state_ == UPLOAD_STOPPED) {
    page_upload_->StartUpload();
  }
}

void PageSyncImpl::Start() {
  FXL_DCHECK(!started_);
  started_ = true;

  page_download_->StartDownload();
  if (enable_upload_) {
    page_upload_->StartUpload();
  }
}

void PageSyncImpl::SetOnIdle(fxl::Closure on_idle) {
  FXL_DCHECK(!on_idle_);
  FXL_DCHECK(!started_);
  on_idle_ = std::move(on_idle);
}

bool PageSyncImpl::IsIdle() {
  return page_upload_->IsIdle() && page_download_->IsIdle();
}

void PageSyncImpl::SetOnBacklogDownloaded(fxl::Closure on_backlog_downloaded) {
  FXL_DCHECK(!on_backlog_downloaded_);
  FXL_DCHECK(!started_);
  on_backlog_downloaded_ = on_backlog_downloaded;
}

void PageSyncImpl::SetSyncWatcher(SyncStateWatcher* watcher) {
  page_watcher_ = watcher;
  if (page_watcher_) {
    page_watcher_->Notify(download_state_, upload_state_);
  }
}

void PageSyncImpl::HandleError() {
  if (error_callback_already_called_) {
    return;
  }

  if (on_error_) {
    error_callback_already_called_ = true;
    on_error_();
  }
}

void PageSyncImpl::CheckIdle() {
  if (IsIdle()) {
    if (on_idle_) {
      on_idle_();
    }
  }
}

void PageSyncImpl::NotifyStateWatcher() {
  if (ledger_watcher_) {
    ledger_watcher_->Notify(download_state_, upload_state_);
  }
  if (page_watcher_) {
    page_watcher_->Notify(download_state_, upload_state_);
  }
  CheckIdle();
}

void PageSyncImpl::SetDownloadState(DownloadSyncState next_download_state) {
  if (next_download_state == DOWNLOAD_PERMANENT_ERROR) {
    HandleError();
  }

  if (download_state_ == DOWNLOAD_BACKLOG &&
      next_download_state != DOWNLOAD_PERMANENT_ERROR &&
      on_backlog_downloaded_) {
    on_backlog_downloaded_();
  }

  if (download_state_ != DOWNLOAD_IDLE &&
      next_download_state == DOWNLOAD_IDLE && enable_upload_) {
    page_upload_->StartUpload();
  }

  download_state_ = next_download_state;
  NotifyStateWatcher();
}

void PageSyncImpl::SetUploadState(UploadSyncState next_upload_state) {
  if (next_upload_state == UPLOAD_PERMANENT_ERROR) {
    HandleError();
  }

  upload_state_ = next_upload_state;
  NotifyStateWatcher();
}

bool PageSyncImpl::IsDownloadIdle() { return page_download_->IsIdle(); }

}  // namespace cloud_sync
