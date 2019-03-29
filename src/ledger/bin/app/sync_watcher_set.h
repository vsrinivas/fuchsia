// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_SYNC_WATCHER_SET_H_
#define SRC_LEDGER_BIN_APP_SYNC_WATCHER_SET_H_

#include <lib/callback/auto_cleanable.h>
#include <src/lib/fxl/macros.h>

#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/sync_coordinator/public/sync_state_watcher.h"

namespace ledger {

class SyncWatcherSet : public sync_coordinator::SyncStateWatcher {
 public:
  SyncWatcherSet();
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

  FXL_DISALLOW_COPY_AND_ASSIGN(SyncWatcherSet);
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_SYNC_WATCHER_SET_H_
