// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/testing/stubs/utc_clock_ready_watcher.h"

namespace forensics::stubs {

void UtcClockReadyWatcher::OnClockReady(::fit::callback<void()> callback) {
  if (is_utc_clock_ready_) {
    callback();
    return;
  }

  callbacks_.push_back(std::move(callback));
}

bool UtcClockReadyWatcher::IsUtcClockReady() const { return is_utc_clock_ready_; }

void UtcClockReadyWatcher::StartClock() {
  // |is_utc_clock_ready_| must be set to true before callbacks are run in case
  // any of them use IsUtcClockReady.
  is_utc_clock_ready_ = true;

  for (auto& callback : callbacks_) {
    callback();
  }
  callbacks_.clear();
}

}  // namespace forensics::stubs
