// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_PAGE_SYNC_IMPL_H_
#define SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_PAGE_SYNC_IMPL_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>

#include <functional>
#include <queue>

#include "src/ledger/bin/cloud_sync/impl/batch_download.h"
#include "src/ledger/bin/cloud_sync/impl/batch_upload.h"
#include "src/ledger/bin/cloud_sync/impl/page_download.h"
#include "src/ledger/bin/cloud_sync/impl/page_upload.h"
#include "src/ledger/bin/cloud_sync/public/page_sync.h"
#include "src/ledger/bin/cloud_sync/public/sync_state_watcher.h"
#include "src/ledger/bin/encryption/public/encryption_service.h"
#include "src/ledger/bin/storage/public/commit_watcher.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/lib/coroutine/coroutine.h"
#include "src/lib/backoff/backoff.h"
#include "src/lib/callback/destruction_sentinel.h"
#include "src/lib/callback/scoped_task_runner.h"
#include "src/lib/fxl/memory/ref_ptr.h"

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
// the page sync to stop, in which case the client is notified using the error
// callback set via SetOnUnrecoverableError().
class PageSyncImpl : public PageSync,
                     public PageDownload::Delegate,
                     public PageUpload::Delegate,
                     public storage::PageSyncDelegate {
 public:
  PageSyncImpl(async_dispatcher_t* dispatcher, coroutine::CoroutineService* coroutine_service,
               storage::PageStorage* storage, storage::PageSyncClient* sync_client,
               encryption::EncryptionService* encryption_service,
               cloud_provider::PageCloudPtr page_cloud,
               std::unique_ptr<backoff::Backoff> download_backoff,
               std::unique_ptr<backoff::Backoff> upload_backoff,
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

  void SetOnPaused(fit::closure on_paused) override;

  bool IsPaused() override;

  void SetOnBacklogDownloaded(fit::closure on_backlog_downloaded) override;

  void SetSyncWatcher(SyncStateWatcher* watcher) override;

  void SetOnUnrecoverableError(fit::closure on_unrecoverable_error) override;

  // Notify the state watcher of a change of synchronization state.
  // PageDownload::Delegate:
  void SetDownloadState(DownloadSyncState next_download_state) override;
  // PageUpload::Delegate:
  void SetUploadState(UploadSyncState next_upload_state) override;
  bool IsDownloadIdle() override;

  // storage::PageSyncDelegate:
  void GetObject(storage::ObjectIdentifier object_identifier,
                 storage::RetrievedObjectType retrieved_object_type,
                 fit::function<void(ledger::Status, storage::ChangeSource, storage::IsObjectSynced,
                                    std::unique_ptr<storage::DataSource::DataChunk>)>
                     callback) override;
  void GetDiff(
      storage::CommitId commit_id, std::vector<storage::CommitId> possible_bases,
      fit::function<void(ledger::Status, storage::CommitId, std::vector<storage::EntryChange>)>
          callback) override;
  void UpdateClock(storage::Clock clock, fit::function<void(ledger::Status)> callback) override;

 private:
  // This may destruct the object.
  void HandleError();

  // This may destruct the object.
  void CheckPaused();

  // This may destruct the object.
  void NotifyStateWatcher();

  coroutine::CoroutineService* const coroutine_service_;
  storage::PageStorage* const storage_;
  storage::PageSyncClient* const sync_client_;
  encryption::EncryptionService* const encryption_service_;
  cloud_provider::PageCloudPtr page_cloud_;
  const fit::closure on_error_;
  const std::string log_prefix_;

  std::unique_ptr<PageDownload> page_download_;
  std::unique_ptr<PageUpload> page_upload_;

  fit::closure on_paused_;
  fit::closure on_backlog_downloaded_;
  fit::closure on_unrecoverable_error_;
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

  callback::DestructionSentinel sentinel_;

  // Must be the last member field.
  callback::ScopedTaskRunner task_runner_;
};

}  // namespace cloud_sync

#endif  // SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_PAGE_SYNC_IMPL_H_
