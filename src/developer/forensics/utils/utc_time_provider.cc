// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/utc_time_provider.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/forensics/utils/time.h"

namespace forensics {

UtcTimeProvider::UtcTimeProvider(std::shared_ptr<sys::ServiceDirectory> services,
                                 timekeeper::Clock* clock)
    : services_(services), clock_(clock), utc_(services_->Connect<fuchsia::time::Utc>()) {
  utc_.set_error_handler([](const zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Lost connection with fuchsia.time.Utc";
  });

  WatchForAccurateUtcTime();
}

std::optional<zx::time_utc> UtcTimeProvider::CurrentTime() const {
  if (!is_utc_time_accurate_) {
    return std::nullopt;
  }

  return CurrentUtcTimeRaw(clock_);
}

void UtcTimeProvider::WatchForAccurateUtcTime() {
  utc_->WatchState([this](const fuchsia::time::UtcState& state) {
    switch (state.source()) {
      case fuchsia::time::UtcSource::UNVERIFIED:
      case fuchsia::time::UtcSource::EXTERNAL:
        is_utc_time_accurate_ = true;
        utc_.Unbind();
        break;
      case fuchsia::time::UtcSource::BACKSTOP:
        // fuchsia.time.Utc does not currently distinguish between devices that have an internal
        // clock and those that do not. So, if a device has an internal clock, it's possbile that
        // the device's UTC time is be accurate despite |BACKSTOP| being returned,
        WatchForAccurateUtcTime();
        break;
    }
  });
}

}  // namespace forensics
