// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/system_monitor/bin/harvester/fuchsia_clock.h"

#include <lib/syslog/cpp/macros.h>

namespace harvester {

std::optional<timekeeper::time_utc> FuchsiaClock::now() {
  if (started_) {
    timekeeper::time_utc now_utc;
    zx_status_t status = clock_->UtcNow(&now_utc);
    if (status == ZX_OK) {
      return now_utc;
    }
  }
  return std::nullopt;
}

std::optional<zx_time_t> FuchsiaClock::nanoseconds() {
  auto now_utc = now();
  if (now_utc.has_value()) {
    return now_utc.value().get();
  }
  return std::nullopt;
}

void FuchsiaClock::WaitForStart(std::function<void(zx_status_t)> callback) {
  FX_LOGS(INFO) << "Checking the state of the system clock.";

  const zx_status_t already_started = zx_object_wait_one(
      clock_handle_->get_handle(), ZX_CLOCK_STARTED, 0u, NULL);

  if (already_started == ZX_OK) {
    FX_LOGS(INFO) << "Clock has been initialized, not waiting.";
    started_ = true;
    callback(ZX_OK);
    return;
  }

  if (started_callback_.has_value()) {
    FX_LOGS(ERROR) << "Started callback already set, replacing current value.";
  }
  started_callback_ = callback;

  const zx_status_t status = started_wait_.Begin(dispatcher_);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to wait for clock start: " << status;
  }
}

void FuchsiaClock::OnClockStarted(async_dispatcher_t* dispatcher,
                                  async::WaitBase* wait, zx_status_t status,
                                  const zx_packet_signal_t* signal) {
  if (status == ZX_ERR_CANCELED) {
    FX_LOGS(ERROR) << "Waiting for clock initialization was canceled.";
    if (started_callback_.has_value()) {
      (started_callback_.value())(status);
    }
    return;
  }

  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to wait for clock initiialization, trying again.";
    wait->Begin(dispatcher);
    return;
  }

  started_ = true;
  if (started_callback_.has_value()) {
    (started_callback_.value())(status);
  }
  FX_LOGS(INFO) << "Clock has been initialized.";
}

}  // namespace harvester
