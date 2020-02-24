// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/utils/clock.h"

#include "src/lib/syslog/cpp/logger.h"

namespace cobalt {

FuchsiaSystemClock::FuchsiaSystemClock(
    const std::shared_ptr<sys::ServiceDirectory>& service_directory) {
  service_directory->Connect(utc_.NewRequest());
}

std::optional<std::chrono::system_clock::time_point> FuchsiaSystemClock::now() {
  if (accurate_) {
    return std::chrono::system_clock::now();
  }
  return std::nullopt;
}

void FuchsiaSystemClock::AwaitExternalSource(std::function<void()> callback) {
  callback_ = std::move(callback);
  FX_LOGS(INFO) << "Making initial call to check the state of the system clock";
  utc_->WatchState([this](fuchsia::time::UtcState utc_state) { WatchStateCallback(utc_state); });
}

void FuchsiaSystemClock::WatchStateCallback(const fuchsia::time::UtcState& utc_state) {
  if (utc_state.source() == fuchsia::time::UtcSource::EXTERNAL) {
    FX_LOGS(INFO) << "Clock has been initialized from an external source";
    accurate_ = true;
    callback_();
  } else {
    FX_LOGS(INFO) << "Clock is not accurate yet, "
                  << "making another call to check the state of the "
                  << "system clock (this should block)";
    utc_->WatchState([this](fuchsia::time::UtcState utc_state) { WatchStateCallback(utc_state); });
  }
}

}  // namespace cobalt
