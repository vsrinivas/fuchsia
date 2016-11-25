// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_PAGE_SYNC_IMPL_H_
#define APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_PAGE_SYNC_IMPL_H_

#include <functional>
#include <queue>

#include "apps/ledger/src/backoff/backoff.h"
#include "apps/ledger/src/cloud_provider/public/cloud_provider.h"
#include "apps/ledger/src/cloud_provider/public/commit_watcher.h"
#include "apps/ledger/src/cloud_sync/impl/commit_download.h"
#include "apps/ledger/src/cloud_sync/impl/commit_upload.h"
#include "apps/ledger/src/cloud_sync/public/page_sync.h"
#include "apps/ledger/src/storage/public/commit_watcher.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "apps/ledger/src/storage/public/page_sync_delegate.h"
#include "lib/ftl/memory/ref_ptr.h"
#include "lib/ftl/memory/weak_ptr.h"
#include "lib/ftl/tasks/task_runner.h"
#include "lib/ftl/time/time_delta.h"

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
// connectivity, thread this signal through CloudProvider and use it as a signal
// to trigger retries.
//
// Unrecoverable errors (such as internal errors accessing the storage) cause
// the page sync to stop, in which case the client is notified using the given
// error callback.
class PageSyncImpl : public PageSync,
                     public storage::CommitWatcher,
                     public storage::PageSyncDelegate,
                     public cloud_provider::CommitWatcher {
 public:
  PageSyncImpl(ftl::RefPtr<ftl::TaskRunner> task_runner,
               storage::PageStorage* storage,
               cloud_provider::CloudProvider* cloud_provider,
               std::unique_ptr<backoff::Backoff> backoff,
               ftl::Closure on_error);
  ~PageSyncImpl() override;

  // PageSync:
  void Start() override;

  void SetOnIdle(ftl::Closure on_idle) override;

  bool IsIdle() override;

  void SetOnBacklogDownloaded(ftl::Closure on_backlog_downloaded) override;

  // storage::CommitWatcher:
  void OnNewCommits(
      const std::vector<std::unique_ptr<const storage::Commit>>& commits,
      storage::ChangeSource source) override;

  // storage::PageSyncDelegate:
  void GetObject(
      storage::ObjectIdView object_id,
      std::function<void(storage::Status status,
                         uint64_t size,
                         mx::datapipe_consumer data)> callback) override;

  // cloud_provider::CommitWatcher:
  void OnRemoteCommit(cloud_provider::Commit commit,
                      std::string timestamp) override;

  void OnError() override;

 private:
  void TryStartDownload();

  void EnqueueDownload(std::vector<cloud_provider::Record> record,
                       ftl::Closure on_done);

  void EnqueueUpload(std::unique_ptr<const storage::Commit> commit);

  void HandleError(const char error_description[]);

  void CheckIdle();

  void BacklogDownloaded();

  ftl::RefPtr<ftl::TaskRunner> task_runner_;
  storage::PageStorage* const storage_;
  cloud_provider::CloudProvider* const cloud_provider_;
  const std::unique_ptr<backoff::Backoff> backoff_;
  const ftl::Closure on_error_;

  ftl::Closure on_idle_;
  ftl::Closure on_backlog_downloaded_;
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

  // A queue of pending commit uploads.
  std::queue<CommitUpload> commit_uploads_;
  // A queue of pending commit downloads.
  std::queue<CommitDownload> commit_downloads_;

  // Must be the last member field.
  ftl::WeakPtrFactory<PageSyncImpl> weak_factory_;
};

}  // namespace cloud_sync

#endif  // APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_PAGE_SYNC_IMPL_H_
