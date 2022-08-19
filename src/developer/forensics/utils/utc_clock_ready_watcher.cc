// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/utc_clock_ready_watcher.h"

#include <lib/syslog/cpp/macros.h>

namespace forensics {

UtcClockReadyWatcher::UtcClockReadyWatcher(async_dispatcher_t* dispatcher,
                                           zx::unowned_clock clock_handle)
    : wait_for_clock_start_(this, clock_handle->get_handle(), ZX_CLOCK_STARTED, /*options=*/0) {
  if (const zx_status_t status = wait_for_clock_start_.Begin(dispatcher); status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "Failed to wait for clock start";
  }
}

void UtcClockReadyWatcher::OnClockReady(::fit::callback<void()> callback) {
  if (is_utc_clock_ready_) {
    callback();
    return;
  }

  callbacks_.push_back(std::move(callback));
}

bool UtcClockReadyWatcher::IsUtcClockReady() const { return is_utc_clock_ready_; }

void UtcClockReadyWatcher::OnClockStart(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                        zx_status_t status, const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    FX_PLOGS(WARNING, status) << "Wait for clock start completed with error, trying again";

    // Attempt to wait for the clock to start again.
    wait->Begin(dispatcher);
    return;
  }

  // |is_utc_clock_ready_| must be set to true before callbacks are run in case
  // any of them use IsUtcClockReady.
  is_utc_clock_ready_ = true;

  for (auto& callback : callbacks_) {
    callback();
  }
  callbacks_.clear();
}

}  // namespace forensics
