// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/utils/clock.h"

#include <lib/syslog/cpp/macros.h>

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
  FX_LOGS(INFO) << "Making initial call to check the state of the system clock";
  WatchExternalSource(std::move(callback));
}

void FuchsiaSystemClock::WatchExternalSource(std::function<void()> callback) {
  utc_->WatchState([this, callback = std::move(callback)](const fuchsia::time::UtcState& state) {
    switch (state.source()) {
      case fuchsia::time::UtcSource::UNVERIFIED:
        FX_LOGS(INFO) << "Clock has been initialized from an unverified source";
        accurate_ = true;
        utc_.Unbind();
        callback();
        break;
      case fuchsia::time::UtcSource::EXTERNAL:
        FX_LOGS(INFO) << "Clock has been initialized from an external source";
        accurate_ = true;
        utc_.Unbind();
        callback();
        break;
      case fuchsia::time::UtcSource::BACKSTOP:
        FX_LOGS(INFO) << "Clock is not accurate yet, "
                      << "making another call to check the state of the "
                      << "system clock. Expect response when clock becomes accurate.";
        WatchExternalSource(callback);
        break;
    }
  });
}

}  // namespace cobalt
