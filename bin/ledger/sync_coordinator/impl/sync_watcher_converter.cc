// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/sync_coordinator/impl/sync_watcher_converter.h"

#include <utility>

namespace sync_coordinator {
namespace {
DownloadSyncState ConvertToDownloadSyncState(
    cloud_sync::DownloadSyncState download) {
  switch (download) {
    case cloud_sync::DOWNLOAD_NOT_STARTED:
      return DownloadSyncState::DOWNLOAD_PENDING;
    case cloud_sync::DOWNLOAD_BACKLOG:
      return DownloadSyncState::DOWNLOAD_IN_PROGRESS;
    case cloud_sync::DOWNLOAD_TEMPORARY_ERROR:
      return DownloadSyncState::DOWNLOAD_PENDING;
    case cloud_sync::DOWNLOAD_SETTING_REMOTE_WATCHER:
      return DownloadSyncState::DOWNLOAD_IN_PROGRESS;
    case cloud_sync::DOWNLOAD_IDLE:
      return DownloadSyncState::DOWNLOAD_IDLE;
    case cloud_sync::DOWNLOAD_IN_PROGRESS:
      return DownloadSyncState::DOWNLOAD_IN_PROGRESS;
    case cloud_sync::DOWNLOAD_PERMANENT_ERROR:
      return DownloadSyncState::DOWNLOAD_ERROR;
  }
}

UploadSyncState ConvertToUploadSyncState(cloud_sync::UploadSyncState upload) {
  switch (upload) {
    case cloud_sync::UPLOAD_NOT_STARTED:
      return UploadSyncState::UPLOAD_PENDING;
    case cloud_sync::UPLOAD_SETUP:
      return UploadSyncState::UPLOAD_PENDING;
    case cloud_sync::UPLOAD_IDLE:
      return UploadSyncState::UPLOAD_IDLE;
    case cloud_sync::UPLOAD_PENDING:
      return UploadSyncState::UPLOAD_PENDING;
    case cloud_sync::UPLOAD_WAIT_TOO_MANY_LOCAL_HEADS:
      return UploadSyncState::UPLOAD_PENDING;
    case cloud_sync::UPLOAD_WAIT_REMOTE_DOWNLOAD:
      return UploadSyncState::UPLOAD_PENDING;
    case cloud_sync::UPLOAD_TEMPORARY_ERROR:
      return UploadSyncState::UPLOAD_PENDING;
    case cloud_sync::UPLOAD_IN_PROGRESS:
      return UploadSyncState::UPLOAD_IN_PROGRESS;
    case cloud_sync::UPLOAD_PERMANENT_ERROR:
      return UploadSyncState::UPLOAD_ERROR;
  }
}

sync_coordinator::SyncStateWatcher::SyncStateContainer ConvertToSyncState(
    cloud_sync::SyncStateWatcher::SyncStateContainer state) {
  return {ConvertToDownloadSyncState(state.download),
          ConvertToUploadSyncState(state.upload)};
}

}  // namespace

SyncWatcherConverter::SyncWatcherConverter(
    sync_coordinator::SyncStateWatcher* watcher)
    : watcher_(watcher) {}
SyncWatcherConverter::~SyncWatcherConverter() {}

void SyncWatcherConverter::Notify(SyncStateContainer sync_state) {
  watcher_->Notify(ConvertToSyncState(sync_state));
}
}  // namespace sync_coordinator
