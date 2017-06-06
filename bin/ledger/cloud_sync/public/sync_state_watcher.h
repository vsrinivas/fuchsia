// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_SYNC_PUBLIC_SYNC_STATE_WATCHER_H_
#define APPS_LEDGER_SRC_CLOUD_SYNC_PUBLIC_SYNC_STATE_WATCHER_H_

namespace cloud_sync {
// Detail of the download part of the synchronization state.
enum DownloadSyncState {
  DOWNLOAD_IDLE,
  CATCH_UP_DOWNLOAD,
  REMOTE_COMMIT_DOWNLOAD,
  DOWNLOAD_ERROR,
};

// Detail of the upload part of the synchronization state.
enum UploadSyncState {
  UPLOAD_IDLE,
  UPLOAD_PENDING,
  WAIT_CATCH_UP_DOWNLOAD,
  WAIT_TOO_MANY_LOCAL_HEADS,
  WAIT_REMOTE_DOWNLOAD,
  UPLOAD_IN_PROGRESS,
  UPLOAD_ERROR,
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

  // Notify the watcher of a new state.
  virtual void Notify(SyncStateContainer sync_state) = 0;

  // Helper method, equivalent to |Notify| above.
  virtual void Notify(DownloadSyncState download, UploadSyncState upload);
};

bool operator==(const SyncStateWatcher::SyncStateContainer& lhs,
                const SyncStateWatcher::SyncStateContainer& rhs);
bool operator!=(const SyncStateWatcher::SyncStateContainer& lhs,
                const SyncStateWatcher::SyncStateContainer& rhs);

}  // namespace cloud_sync

#endif  // APPS_LEDGER_SRC_CLOUD_SYNC_PUBLIC_SYNC_STATE_WATCHER_H_
