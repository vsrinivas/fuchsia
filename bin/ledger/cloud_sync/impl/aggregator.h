// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_AGGREGATOR_H_
#define PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_AGGREGATOR_H_

#include <memory>
#include <unordered_set>

#include "peridot/bin/ledger/cloud_sync/public/sync_state_watcher.h"
#include "lib/fxl/macros.h"

namespace cloud_sync {

// Aggregator collects notifications from several watchers generated using
// |GetNewStateWatcher| into one notification stream sent to the watcher set in
// the constructor.
class Aggregator {
 public:
  // Sets the base watcher that will receive the aggregated notification stream.
  explicit Aggregator(cloud_sync::SyncStateWatcher* base_watcher);
  ~Aggregator();

  // Generates a new source of notifications for this aggregator. Note that
  // std::unique_ptr<SyncStateWatcher> should remain alive as long as the
  // Aggregator object is alive.
  std::unique_ptr<SyncStateWatcher> GetNewStateWatcher();

 private:
  class Listener;

  void UnregisterListener(Listener* listener);
  void NewStateAvailable();

  SyncStateWatcher::SyncStateContainer state_;

  std::unordered_set<Listener*> listeners_;
  SyncStateWatcher* base_watcher_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Aggregator);
};

}  // namespace cloud_sync

#endif  // PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_AGGREGATOR_H_
