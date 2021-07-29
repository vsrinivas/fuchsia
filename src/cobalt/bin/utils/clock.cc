// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/utils/clock.h"

#include <lib/inspect/cpp/inspect.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/utc.h>

namespace cobalt {

FuchsiaSystemClock::FuchsiaSystemClock(async_dispatcher_t* dispatcher, inspect::Node node)
    : FuchsiaSystemClock(dispatcher, std::move(node), zx::unowned_clock(zx_utc_reference_get())) {}

FuchsiaSystemClock::FuchsiaSystemClock(async_dispatcher_t* dispatcher, inspect::Node node,
                                       zx::unowned_clock clock)
    : dispatcher_(dispatcher),
      system_clock_node_(std::move(node)),
      system_clock_accurate_(system_clock_node_.CreateBool("is_accurate", false)),
      utc_start_wait_(clock->get_handle(), ZX_CLOCK_STARTED, 0) {}

std::optional<std::chrono::system_clock::time_point> FuchsiaSystemClock::now() {
  if (accurate_) {
    return std::chrono::system_clock::now();
  }
  return std::nullopt;
}

void FuchsiaSystemClock::AwaitExternalSource(std::function<void()> callback) {
  time_t current_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  FX_LOGS(INFO) << "Waiting for the system clock to become accurate at: "
                << std::put_time(std::localtime(&current_time), "%F %T %z");
  system_clock_node_.CreateInt("start_waiting_time", current_time, &inspect_values_);
  utc_start_wait_.Begin(dispatcher_, [this, callback = std::move(callback)](
                                         async_dispatcher_t* dispatcher, async::WaitOnce* wait,
                                         zx_status_t status, const zx_packet_signal_t* signal) {
    if (status == ZX_ERR_CANCELED) {
      FX_LOGS(ERROR) << "Failed to wait for clock initialization";
      return;
    }

    this->accurate_ = true;
    time_t current_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    FX_LOGS(INFO) << "The system clock has become accurate, now at: "
                  << std::put_time(std::localtime(&current_time), "%F %T %z");
    system_clock_node_.CreateInt("clock_accurate_time", current_time, &inspect_values_);
    system_clock_accurate_.Set(true);
    callback();
  });
}

}  // namespace cobalt
