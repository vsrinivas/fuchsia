// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_CLOUD_SYNC_PUBLIC_PAGE_SYNC_H_
#define SRC_LEDGER_BIN_CLOUD_SYNC_PUBLIC_PAGE_SYNC_H_

#include <lib/fit/function.h>

#include <functional>

#include "src/ledger/bin/cloud_sync/public/sync_state_watcher.h"

namespace cloud_sync {

// Manages cloud sync for a single page.
//
// PageSync is responsible for uploading locally created artifacts (commits and
// objects) of the page from storage to the cloud, and for fetching remote
// artifacts of the same page from the cloud and putting them in storage.
class PageSync {
 public:
  PageSync() = default;
  PageSync(const PageSync&) = delete;
  PageSync& operator=(const PageSync&) = delete;
  virtual ~PageSync() = default;

  // Starts syncing. Upon connection drop, the sync will restart automatically,
  // the client doesn't need to call Start() again.
  virtual void Start() = 0;

  // Sets a callback that will be called after Start() every time when PageSync becomes paused, that
  // is: finished uploading all unsynced local artifacts to the cloud and not downloading any remote
  // artifacts, or backing off after a temporary error. Can be set at most once and only before
  // calling Start().
  virtual void SetOnPaused(fit::closure on_paused) = 0;

  // Returns true iff PageSync is paused, that is with no pending upload or
  // download work, or backing off.
  virtual bool IsPaused() = 0;

  // Sets a callback that will be called at most once after Start(), when all
  // remote commits added to the cloud between the last sync and starting the
  // current sync are added to storage. This can be used by the client to delay
  // exposing the local page until it catches up with the cloud. Can be set at
  // most once and only before calling Start().
  virtual void SetOnBacklogDownloaded(fit::closure on_backlog_downloaded) = 0;

  // Sets a watcher for the synchronization state of this page. Calling the
  // watcher must not destruct the PageSync object.
  virtual void SetSyncWatcher(SyncStateWatcher* watcher) = 0;

  // Sets a callback that will be called at most once, before or after Start(),
  // when PageSync hits an error it cannot recover from. The class can be
  // deleted after that.
  virtual void SetOnUnrecoverableError(fit::closure on_unrecoverable_error) = 0;
};

}  // namespace cloud_sync

#endif  // SRC_LEDGER_BIN_CLOUD_SYNC_PUBLIC_PAGE_SYNC_H_
