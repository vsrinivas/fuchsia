// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_PAGE_SYNC_IMPL_H_
#define PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_PAGE_SYNC_IMPL_H_

#include <functional>
#include <queue>

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/backoff/backoff.h>
#include <lib/callback/scoped_task_runner.h>
#include <lib/fit/function.h>
#include <lib/fxl/memory/ref_ptr.h>
#include <lib/fxl/time/time_delta.h>

#include "peridot/bin/ledger/cloud_sync/impl/batch_download.h"
#include "peridot/bin/ledger/cloud_sync/impl/batch_upload.h"
#include "peridot/bin/ledger/cloud_sync/impl/page_download.h"
#include "peridot/bin/ledger/cloud_sync/impl/page_upload.h"
#include "peridot/bin/ledger/cloud_sync/public/page_sync.h"
#include "peridot/bin/ledger/cloud_sync/public/sync_state_watcher.h"
#include "peridot/bin/ledger/encryption/public/encryption_service.h"
#include "peridot/bin/ledger/storage/public/commit_watcher.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"

namespace cloud_sync {

// Manages cloud sync for a single page.
//
// Contract: commits are uploaded in the same order as storage delivers them.
// The backlog of unsynced commits is uploaded first, then we upload commits
// delivered through storage watcher in the notification order.
//
// Conversely for the remote commits: the backlog of remote commits is
// downloaded first, then a cloud watcher is set to track new remote commits
// appearing in the cloud provider. Remote commits are added to storage in the
// order in which they were added to the cloud provided.
//
// In order to track which remote commits were already fetched, we keep track of
// the server-side timestamp of the last commit we added to storage. As this
// information needs to be persisted through reboots, we store the timestamp
// itself in storage using a dedicated API (Get/SetSyncMetadata()).
//
// Recoverable errors (such as network errors) are automatically retried with
// the given backoff policy, using the given task runner to schedule the tasks.
// TODO(ppi): once the network service can notify us about regained
// connectivity, thread this signal through PageCloudHandler and use it as a
// signal to trigger retries.
//
// Unrecoverable errors (such as internal errors accessing the storage) cause
// the page sync to stop, in which case the client is notified using the given
// error callback.
class PageSyncImpl : public PageSync,
                     public PageDownload::Delegate,
                     public PageUpload::Delegate {
 public:
  PageSyncImpl(async_dispatcher_t* dispatcher, storage::PageStorage* storage,
               storage::PageSyncClient* sync_client,
               encryption::EncryptionService* encryption_service,
               cloud_provider::PageCloudPtr page_cloud,
               std::unique_ptr<backoff::Backoff> download_backoff,
               std::unique_ptr<backoff::Backoff> upload_backoff,
               fit::closure on_error,
               std::unique_ptr<SyncStateWatcher> ledger_watcher = nullptr);
  ~PageSyncImpl() override;

  // |on_delete| will be called when this class is deleted.
  void set_on_delete(fit::function<void()> on_delete) {
    FXL_DCHECK(!on_delete_);
    on_delete_ = std::move(on_delete);
  }

  // Enables upload. Has no effect if this method has already been called.
  void EnableUpload();

  // PageSync:
  void Start() override;

  void SetOnIdle(fit::closure on_idle) override;

  bool IsIdle() override;

  void SetOnBacklogDownloaded(fit::closure on_backlog_downloaded) override;

  void SetSyncWatcher(SyncStateWatcher* watcher) override;

 private:
  void HandleError();

  void CheckIdle();

  // Notify the state watcher of a change of synchronization state.
  void SetDownloadState(DownloadSyncState next_download_state) override;
  void SetUploadState(UploadSyncState next_upload_state) override;
  bool IsDownloadIdle() override;

  void NotifyStateWatcher();

  storage::PageStorage* const storage_;
  storage::PageSyncClient* const sync_client_;
  encryption::EncryptionService* const encryption_service_;
  cloud_provider::PageCloudPtr page_cloud_;
  const fit::closure on_error_;
  const std::string log_prefix_;

  std::unique_ptr<PageDownload> page_download_;
  std::unique_ptr<PageUpload> page_upload_;

  fit::closure on_idle_;
  fit::closure on_backlog_downloaded_;
  // Ensures that each instance is started only once.
  bool started_ = false;
  // Set to true on unrecoverable error. This indicates that PageSyncImpl is in
  // broken state.
  bool error_callback_already_called_ = false;
  // Blocks the start of the upload process until we get an explicit signal.
  bool enable_upload_ = false;

  // Called on destruction.
  fit::function<void()> on_delete_;

  // Watcher of the synchronization state that reports to the LedgerSync object.
  std::unique_ptr<SyncStateWatcher> ledger_watcher_;
  SyncStateWatcher* page_watcher_ = nullptr;
  DownloadSyncState download_state_ = DOWNLOAD_NOT_STARTED;
  UploadSyncState upload_state_ = UPLOAD_NOT_STARTED;

  // Must be the last member field.
  callback::ScopedTaskRunner task_runner_;
};

}  // namespace cloud_sync

#endif  // PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_PAGE_SYNC_IMPL_H_
