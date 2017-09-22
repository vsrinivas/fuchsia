// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_SYNC_WATCHER_SET_H_
#define APPS_LEDGER_SRC_APP_SYNC_WATCHER_SET_H_

#include "lib/ledger/fidl/ledger.fidl.h"
#include "peridot/bin/ledger/callback/auto_cleanable.h"
#include "peridot/bin/ledger/cloud_sync/public/sync_state_watcher.h"
#include "lib/fxl/macros.h"

namespace ledger {

class SyncWatcherSet : public cloud_sync::SyncStateWatcher {
 public:
  SyncWatcherSet();
  ~SyncWatcherSet() override;

  // Adds a new SyncWatcher.
  void AddSyncWatcher(fidl::InterfaceHandle<SyncWatcher> watcher);

  // Notify the client watchers of a new state.
  void Notify(SyncStateContainer sync_state) override;

  using cloud_sync::SyncStateWatcher::Notify;

 private:
  class SyncWatcherContainer;

  void SendIfPending();

  SyncStateContainer current_;
  callback::AutoCleanableSet<SyncWatcherContainer> watchers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SyncWatcherSet);
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_SYNC_WATCHER_SET_H_
