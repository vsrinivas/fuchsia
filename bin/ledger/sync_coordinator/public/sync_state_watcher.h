// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_SYNC_COORDINATOR_PUBLIC_SYNC_STATE_WATCHER_H_
#define PERIDOT_BIN_LEDGER_SYNC_COORDINATOR_PUBLIC_SYNC_STATE_WATCHER_H_

namespace sync_coordinator {
// Detail of the download part of the synchronization state.
enum DownloadSyncState {
  DOWNLOAD_IDLE = 0,
  DOWNLOAD_PENDING,
  DOWNLOAD_IN_PROGRESS,
  DOWNLOAD_ERROR,
};

// Detail of the upload part of the synchronization state.
enum UploadSyncState {
  UPLOAD_IDLE = 0,
  UPLOAD_PENDING,
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
  };

  virtual ~SyncStateWatcher() {}

  // Notifies the watcher of a new state.
  virtual void Notify(SyncStateContainer sync_state) = 0;
};
}  // namespace sync_coordinator

#endif  // PERIDOT_BIN_LEDGER_SYNC_COORDINATOR_PUBLIC_SYNC_STATE_WATCHER_H_
