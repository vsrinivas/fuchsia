// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/cloud_sync/public/sync_state_watcher.h"

#include <tuple>

namespace cloud_sync {

SyncStateWatcher::SyncStateContainer::SyncStateContainer(
    DownloadSyncState download, UploadSyncState upload)
    : download(download), upload(upload) {}

SyncStateWatcher::SyncStateContainer::SyncStateContainer() {}

void SyncStateWatcher::SyncStateContainer::Merge(SyncStateContainer other) {
  if (other.download > this->download) {
    download = other.download;
  }
  if (other.upload > this->upload) {
    upload = other.upload;
  }
}

void SyncStateWatcher::Notify(DownloadSyncState download,
                              UploadSyncState upload) {
  Notify(SyncStateContainer(download, upload));
}

bool operator==(const SyncStateWatcher::SyncStateContainer& lhs,
                const SyncStateWatcher::SyncStateContainer& rhs) {
  return std::tie(lhs.download, lhs.upload) ==
         std::tie(rhs.download, rhs.upload);
}

bool operator!=(const SyncStateWatcher::SyncStateContainer& lhs,
                const SyncStateWatcher::SyncStateContainer& rhs) {
  return !(lhs == rhs);
}

std::ostream& operator<<(
    std::ostream& strm,
    const SyncStateWatcher::SyncStateContainer& sync_state) {
  return strm << "{" << sync_state.download << ", " << sync_state.upload << "}";
}

}  // namespace cloud_sync
