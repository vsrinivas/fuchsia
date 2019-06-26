// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/sync_helper/sync_helper.h"

namespace ledger {

SyncHelper::SyncHelper() : current_sync_point_(0), weak_ptr_factory_(this) {}

void SyncHelper::RegisterSynchronizationCallback(fit::function<void()> callback) {
  if (in_flight_operation_counts_per_sync_point_.empty()) {
    callback();
    return;
  }

  sync_callback_per_sync_points_[current_sync_point_] = std::move(callback);
  ++current_sync_point_;
  in_flight_operation_counts_per_sync_point_[current_sync_point_] = 0;
}

void SyncHelper::CallSynchronizationCallbacks() {
  for (auto it = in_flight_operation_counts_per_sync_point_.begin();
       it != in_flight_operation_counts_per_sync_point_.end() && it->second == 0;
       it = in_flight_operation_counts_per_sync_point_.erase(it)) {
    auto sync_point_it = sync_callback_per_sync_points_.find(it->first);
    if (sync_point_it != sync_callback_per_sync_points_.end()) {
      sync_point_it->second();
      sync_callback_per_sync_points_.erase(sync_point_it);
    }
  }
  if (empty() && on_empty_callback_) {
    on_empty_callback_();
  }
}

}  // namespace ledger
