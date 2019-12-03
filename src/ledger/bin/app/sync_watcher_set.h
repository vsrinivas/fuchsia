// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_SYNC_WATCHER_SET_H_
#define SRC_LEDGER_BIN_APP_SYNC_WATCHER_SET_H_

#include "lib/async/dispatcher.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/sync_coordinator/public/sync_state_watcher.h"
#include "src/lib/callback/auto_cleanable.h"

namespace ledger {

class SyncWatcherSet : public sync_coordinator::SyncStateWatcher {
 public:
  SyncWatcherSet(async_dispatcher_t* dispatcher);
  SyncWatcherSet(const SyncWatcherSet&) = delete;
  SyncWatcherSet& operator=(const SyncWatcherSet&) = delete;
  ~SyncWatcherSet() override;

  // Adds a new SyncWatcher.
  void AddSyncWatcher(fidl::InterfaceHandle<SyncWatcher> watcher);

  // Notify the client watchers of a new state.
  void Notify(SyncStateContainer sync_state) override;

 private:
  class SyncWatcherContainer;

  void SendIfPending();

  SyncStateContainer current_;
  callback::AutoCleanableSet<SyncWatcherContainer> watchers_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_SYNC_WATCHER_SET_H_
