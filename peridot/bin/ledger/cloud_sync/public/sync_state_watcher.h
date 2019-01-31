// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_CLOUD_SYNC_PUBLIC_SYNC_STATE_WATCHER_H_
#define PERIDOT_BIN_LEDGER_CLOUD_SYNC_PUBLIC_SYNC_STATE_WATCHER_H_

#include <ostream>

namespace cloud_sync {
// Detail of the download part of the synchronization state.
enum DownloadSyncState {
  // Download has not started.
  // Possible successor states: DOWNLOAD_BACKLOG.
  DOWNLOAD_NOT_STARTED = 0,
  // Download is downloading the commit backlog.
  // Possible successor states: DOWNLOAD_TEMPORARY_ERROR,
  // DOWNLOAD_SETTING_REMOTE_WATCHER.
  DOWNLOAD_BACKLOG,
  // Download experienced a temporary error and will attempt to recover
  // automatically.
  // Possible successor states: DOWNLOAD_BACKLOG,
  // DOWNLOAD_SETTING_REMOTE_WATCHER.
  DOWNLOAD_TEMPORARY_ERROR,
  // Download prepares the remote watcher to be notified of new remote commits.
  // Possible successor states: DOWNLOAD_IDLE.
  DOWNLOAD_SETTING_REMOTE_WATCHER,
  // Download is idle and waits for new remote commits to download.
  // Possible successor states: DOWNLOAD_TEMPORARY_ERROR, DOWNLOAD_IN_PROGRESS,
  // DOWNLOAD_PERMANENT_ERROR.
  DOWNLOAD_IDLE,
  // Download is in progress.
  // Possible successor states: DOWNLOAD_TEMPORARY_ERROR, DOWNLOAD_IDLE,
  // DOWNLOAD_PERMANENT_ERROR.
  DOWNLOAD_IN_PROGRESS,
  // Download experienced a permanent, unrecoverable error.
  // Possible successor states: None.
  DOWNLOAD_PERMANENT_ERROR,
};

// Detail of the upload part of the synchronization state.
enum UploadSyncState {
  // Upload has not started.
  // Possible successor states: UPLOAD_SETUP.
  UPLOAD_NOT_STARTED = 0,
  // Upload is started and being prepared.
  // Possible successor states: UPLOAD_IDLE, UPLOAD_WAIT_TOO_MANY_LOCAL_HEADS,
  // UPLOAD_WAIT_REMOTE_DOWNLOAD, UPLOAD_PERMANENT_ERROR.
  UPLOAD_SETUP,
  // Upload is ready, but currently idle.
  // Possible successor states: UPLOAD_PENDING,
  // UPLOAD_WAIT_TOO_MANY_LOCAL_HEADS,
  // UPLOAD_WAIT_REMOTE_DOWNLOAD, UPLOAD_PERMANENT_ERROR.
  UPLOAD_IDLE,
  // Upload has some contents to upload, but has to wait before proceeding.
  // Possible successor states: UPLOAD_WAIT_TOO_MANY_LOCAL_HEADS,
  // UPLOAD_WAIT_REMOTE_DOWNLOAD, UPLOAD_PERMANENT_ERROR.
  UPLOAD_PENDING,
  // Upload cannot proceed as there are more than one local head commit.
  // Possible successor states: UPLOAD_IDLE, UPLOAD_IN_PROGRESS,
  // UPLOAD_WAIT_REMOTE_DOWNLOAD, UPLOAD_PERMANENT_ERROR.
  UPLOAD_WAIT_TOO_MANY_LOCAL_HEADS,
  // Upload is waiting for a remote download to finish.
  // Possible successor states: UPLOAD_IDLE, UPLOAD_IN_PROGRESS,
  // UPLOAD_WAIT_TOO_MANY_LOCAL_HEADS, UPLOAD_PERMANENT_ERROR.
  UPLOAD_WAIT_REMOTE_DOWNLOAD,
  // Upload experienced a temporary error and will attempt to recover
  // automatically.
  // Possible successor states: UPLOAD_IDLE, UPLOAD_WAIT_TOO_MANY_LOCAL_HEADS,
  // UPLOAD_WAIT_REMOTE_DOWNLOAD, UPLOAD_IN_PROGRESS, UPLOAD_PERMANENT_ERROR.
  UPLOAD_TEMPORARY_ERROR,
  // Upload is uploading a local commit and its contents.
  // Possible successor states: UPLOAD_IDLE, UPLOAD_WAIT_TOO_MANY_LOCAL_HEADS,
  // UPLOAD_WAIT_REMOTE_DOWNLOAD, UPLOAD_PERMANENT_ERROR.
  UPLOAD_IN_PROGRESS,
  // Upload has experienced an unrecoverable error and cannot continue.
  // Possible successor states: None.
  UPLOAD_PERMANENT_ERROR,
};

// Watcher interface for the current state of data synchronization
class SyncStateWatcher {
 public:
  // Container for the synchronization state, containing both download and
  // upload components.
  struct SyncStateContainer {
    DownloadSyncState download = DOWNLOAD_IDLE;
    UploadSyncState upload = UPLOAD_IDLE;

    SyncStateContainer(DownloadSyncState download, UploadSyncState upload);
    SyncStateContainer();

    void Merge(SyncStateContainer other);
  };

  virtual ~SyncStateWatcher() {}

  // Notifies the watcher of a new state.
  virtual void Notify(SyncStateContainer sync_state) = 0;

  // Helper method, equivalent to |Notify| above.
  virtual void Notify(DownloadSyncState download, UploadSyncState upload);
};

bool operator==(const SyncStateWatcher::SyncStateContainer& lhs,
                const SyncStateWatcher::SyncStateContainer& rhs);
bool operator!=(const SyncStateWatcher::SyncStateContainer& lhs,
                const SyncStateWatcher::SyncStateContainer& rhs);
std::ostream& operator<<(
    std::ostream& strm, const SyncStateWatcher::SyncStateContainer& sync_state);

}  // namespace cloud_sync

#endif  // PERIDOT_BIN_LEDGER_CLOUD_SYNC_PUBLIC_SYNC_STATE_WATCHER_H_
