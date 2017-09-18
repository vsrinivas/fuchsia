// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_PAGE_SYNC_IMPL_H_
#define APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_PAGE_SYNC_IMPL_H_

#include <functional>
#include <queue>

#include "apps/ledger/src/auth_provider/auth_provider.h"
#include "apps/ledger/src/backoff/backoff.h"
#include "apps/ledger/src/callback/cancellable.h"
#include "apps/ledger/src/callback/scoped_task_runner.h"
#include "apps/ledger/src/cloud_provider/public/commit_watcher.h"
#include "apps/ledger/src/cloud_provider/public/page_cloud_handler.h"
#include "apps/ledger/src/cloud_sync/impl/batch_download.h"
#include "apps/ledger/src/cloud_sync/impl/batch_upload.h"
#include "apps/ledger/src/cloud_sync/public/page_sync.h"
#include "apps/ledger/src/cloud_sync/public/sync_state_watcher.h"
#include "apps/ledger/src/storage/public/commit_watcher.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "apps/ledger/src/storage/public/page_sync_delegate.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/fxl/time/time_delta.h"

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
                     public storage::CommitWatcher,
                     public storage::PageSyncDelegate,
                     public cloud_provider_firebase::CommitWatcher {
 public:
  PageSyncImpl(fxl::RefPtr<fxl::TaskRunner> task_runner,
               storage::PageStorage* storage,
               cloud_provider_firebase::PageCloudHandler* cloud_provider,
               auth_provider::AuthProvider* auth_provider,
               std::unique_ptr<backoff::Backoff> backoff,
               fxl::Closure on_error,
               std::unique_ptr<SyncStateWatcher> ledger_watcher = nullptr);
  ~PageSyncImpl() override;

  // |on_delete| will be called when this class is deleted.
  void set_on_delete(std::function<void()> on_delete) {
    FXL_DCHECK(!on_delete_);
    on_delete_ = on_delete;
  }

  // Enables upload. Has no effect if this method has already been called.
  void EnableUpload();

  // PageSync:
  void Start() override;

  void SetOnIdle(fxl::Closure on_idle) override;

  bool IsIdle() override;

  void SetOnBacklogDownloaded(fxl::Closure on_backlog_downloaded) override;

  void SetSyncWatcher(SyncStateWatcher* watcher) override;

  // storage::CommitWatcher:
  void OnNewCommits(
      const std::vector<std::unique_ptr<const storage::Commit>>& commits,
      storage::ChangeSource source) override;

  // storage::PageSyncDelegate:
  void GetObject(storage::ObjectIdView object_id,
                 std::function<void(storage::Status status,
                                    uint64_t size,
                                    zx::socket data)> callback) override;

  // cloud_provider_firebase::CommitWatcher:
  void OnRemoteCommits(
      std::vector<cloud_provider_firebase::Record> records) override;

  void OnConnectionError() override;

  void OnTokenExpired() override;

  void OnMalformedNotification() override;

 private:
  // Downloads the initial backlog of remote commits, and sets up the remote
  // watcher upon success.
  void StartDownload();

  // Uploads the initial backlog of local unsynced commits, and sets up the
  // storage watcher upon success.
  void StartUpload();

  // Downloads the given batch of commits.
  void DownloadBatch(std::vector<cloud_provider_firebase::Record> records,
                     fxl::Closure on_done);

  void SetRemoteWatcher(bool is_retry);

  void UploadUnsyncedCommits();
  void VerifyUnsyncedCommits(
      std::vector<std::unique_ptr<const storage::Commit>> commits);
  void HandleUnsyncedCommits(
      std::vector<std::unique_ptr<const storage::Commit>> commits);

  void UploadStagedCommits();

  void HandleError(const char error_description[]);

  void CheckIdle();

  void BacklogDownloaded();

  // Schedules the given closure to execute after the delay determined by
  // |backoff_|, but only if |this| still is valid and |errored_| is not set.
  void Retry(fxl::Closure callable);

  // Notify the state watcher of a change of synchronization state.
  void NotifyStateWatcher();
  void SetDownloadState(DownloadSyncState sync_state);
  void SetUploadState(UploadSyncState sync_state);
  void SetState(DownloadSyncState download_state, UploadSyncState upload_state);

  // Retrieves the auth token from token provider and executes the given
  // callback. Fails hard and stops the sync if the token can't be retrieved.
  void GetAuthToken(std::function<void(std::string)> on_token_ready,
                    fxl::Closure on_failed);

  storage::PageStorage* const storage_;
  cloud_provider_firebase::PageCloudHandler* const cloud_provider_;
  auth_provider::AuthProvider* const auth_provider_;
  const std::unique_ptr<backoff::Backoff> backoff_;
  const fxl::Closure on_error_;
  const std::string log_prefix_;

  fxl::Closure on_idle_;
  fxl::Closure on_backlog_downloaded_;
  // Ensures that each instance is started only once.
  bool started_ = false;
  // Track which watchers are set, so that we know which to unset on hard error.
  bool local_watch_set_ = false;
  bool remote_watch_set_ = false;
  // Set to true on unrecoverable error. This indicates that PageSyncImpl is in
  // broken state.
  bool errored_ = false;
  // Set to true when the backlog of commits to retrieve is downloaded. This
  // ensures that sync is not reported as idle until the commits to be
  // downloaded are retrieved.
  bool download_list_retrieved_ = false;
  // Set to true when upload is enabled.
  bool upload_enabled_ = false;

  // Current batch of local commits being uploaded.
  std::unique_ptr<BatchUpload> batch_upload_;
  // Set to true when there are new commits to upload
  bool commits_to_upload_ = false;
  // The current batch of remote commits being downloaded.
  std::unique_ptr<BatchDownload> batch_download_;
  // Pending remote commits to download.
  std::vector<cloud_provider_firebase::Record> commits_to_download_;
  // Called on destruction.
  std::function<void()> on_delete_;

  // Watcher of the synchronization state that reports to the LedgerSync object.
  std::unique_ptr<SyncStateWatcher> ledger_watcher_;
  SyncStateWatcher* page_watcher_ = nullptr;
  // Download & upload states.
  DownloadSyncState download_state_ = DOWNLOAD_IDLE;
  UploadSyncState upload_state_ = UPLOAD_IDLE;

  // Pending auth token requests to be cancelled when this class goes away.
  callback::CancellableContainer auth_token_requests_;

  // Must be the last member field.
  callback::ScopedTaskRunner task_runner_;
};

}  // namespace cloud_sync

#endif  // APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_PAGE_SYNC_IMPL_H_
