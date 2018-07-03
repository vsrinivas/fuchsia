// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_SYNC_COORDINATOR_PUBLIC_PAGE_SYNC_H_
#define PERIDOT_BIN_LEDGER_SYNC_COORDINATOR_PUBLIC_PAGE_SYNC_H_

#include <functional>

#include <lib/fit/function.h>
#include <lib/fxl/macros.h>

#include "peridot/bin/ledger/sync_coordinator/public/sync_state_watcher.h"

namespace sync_coordinator {

// Manages synchronization of a single page.
//
// PageSync is responsible for uploading locally created artifacts (commits and
// objects) of the page and for fetching remote artifacts of the same page and
// putting them in storage. It manages coordination between upload/download
// throught the cloud and through local peers.
class PageSync {
 public:
  PageSync() {}
  virtual ~PageSync() {}

  // Starts syncing. Upon connection drop, the sync will restart automatically,
  // the client doesn't need to call Start() again.
  virtual void Start() = 0;

  // Sets a callback that will be called after Start() every time when PageSync
  // becomes idle, that is: finished uploading all unsynced local artifacts and
  // not downloading any remote artifacts. Can be set at most once and only
  // before calling Start().
  virtual void SetOnIdle(fit::closure on_idle) = 0;

  // Returns true iff PageSync is idle, that is with no pending upload or
  // download work.
  virtual bool IsIdle() = 0;

  // Sets a callback that will be called at most once after Start(), when all
  // remote commits added to the cloud between the last sync and starting the
  // current sync are added to storage. This can be used by the client to delay
  // exposing the local page until it catches up with the cloud. Can be set at
  // most once and only before calling Start().
  virtual void SetOnBacklogDownloaded(fit::closure on_backlog_downloaded) = 0;

  // Sets a watcher for the synchronization state of this page.
  virtual void SetSyncWatcher(SyncStateWatcher* watcher) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageSync);
};

}  // namespace sync_coordinator

#endif  // PERIDOT_BIN_LEDGER_SYNC_COORDINATOR_PUBLIC_PAGE_SYNC_H_
