// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/utils/clock.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/utc.h>

namespace cobalt {

FuchsiaSystemClock::FuchsiaSystemClock(async_dispatcher_t* dispatcher)
    : FuchsiaSystemClock(dispatcher, zx::unowned_clock(zx_utc_reference_get())) {}

FuchsiaSystemClock::FuchsiaSystemClock(async_dispatcher_t* dispatcher, zx::unowned_clock clock)
    : dispatcher_(dispatcher), utc_start_wait_(clock->get_handle(), ZX_CLOCK_STARTED, 0) {}

std::optional<std::chrono::system_clock::time_point> FuchsiaSystemClock::now() {
  if (accurate_) {
    return std::chrono::system_clock::now();
  }
  return std::nullopt;
}

void FuchsiaSystemClock::AwaitExternalSource(std::function<void()> callback) {
  FX_LOGS(INFO) << "Checking the state of the system clock";
  utc_start_wait_.Begin(dispatcher_, [this, callback = std::move(callback)](
                                         async_dispatcher_t* dispatcher, async::WaitOnce* wait,
                                         zx_status_t status, const zx_packet_signal_t* signal) {
    if (status == ZX_ERR_CANCELED) {
      FX_LOGS(ERROR) << "Failed to wait for clock initiialization";
      return;
    }

    this->accurate_ = true;
    FX_LOGS(INFO) << "Clock has been initialized";
    callback();
  });
}

}  // namespace cobalt
