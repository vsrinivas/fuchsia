// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/utc_time_provider.h"

#include <lib/syslog/cpp/macros.h>

#include <cstdlib>
#include <optional>

#include "src/developer/forensics/utils/time.h"
#include "src/lib/files/file.h"

namespace forensics {

UtcTimeProvider::UtcTimeProvider(std::shared_ptr<sys::ServiceDirectory> services,
                                 timekeeper::Clock* clock)
    : UtcTimeProvider(services, clock, std::nullopt) {}

UtcTimeProvider::UtcTimeProvider(std::shared_ptr<sys::ServiceDirectory> services,
                                 timekeeper::Clock* clock,
                                 PreviousBootFile utc_monotonic_difference_file)
    : UtcTimeProvider(services, clock, std::optional(utc_monotonic_difference_file)) {}

UtcTimeProvider::UtcTimeProvider(std::shared_ptr<sys::ServiceDirectory> services,
                                 timekeeper::Clock* clock,
                                 std::optional<PreviousBootFile> utc_monotonic_difference_file)
    : services_(services),
      clock_(clock),
      utc_(services_->Connect<fuchsia::time::Utc>()),
      utc_monotonic_difference_file_(std::move(utc_monotonic_difference_file)),
      previous_boot_utc_monotonic_difference_(std::nullopt) {
  utc_.set_error_handler([](const zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Lost connection with fuchsia.time.Utc";
  });

  WatchForAccurateUtcTime();

  if (!utc_monotonic_difference_file_.has_value()) {
    return;
  }

  std::string buf;
  if (!files::ReadFileToString(utc_monotonic_difference_file_.value().PreviousBootPath(), &buf)) {
    return;
  }

  previous_boot_utc_monotonic_difference_ =
      zx::duration(strtoll(buf.c_str(), nullptr, /*base*/ 10));
}

std::optional<zx::time_utc> UtcTimeProvider::CurrentTime() const {
  if (!is_utc_time_accurate_) {
    return std::nullopt;
  }

  return CurrentUtcTimeRaw(clock_);
}

std::optional<zx::duration> UtcTimeProvider::CurrentUtcMonotonicDifference() const {
  if (!is_utc_time_accurate_) {
    return std::nullopt;
  }

  if (const std::optional<zx::time_utc> current_utc_time = CurrentUtcTimeRaw(clock_);
      current_utc_time.has_value()) {
    const zx::duration utc_monotonic_difference(current_utc_time.value().get() -
                                                clock_->Now().get());
    if (utc_monotonic_difference_file_.has_value()) {
      // Write the most recent UTC-monotonic difference in case either clock has been adjusted.
      files::WriteFile(utc_monotonic_difference_file_.value().CurrentBootPath(),
                       std::to_string(utc_monotonic_difference.get()));
    }
    return utc_monotonic_difference;
  }

  return std::nullopt;
}

std::optional<zx::duration> UtcTimeProvider::PreviousBootUtcMonotonicDifference() const {
  return previous_boot_utc_monotonic_difference_;
}

void UtcTimeProvider::WatchForAccurateUtcTime() {
  utc_->WatchState([this](const fuchsia::time::UtcState& state) {
    switch (state.source()) {
      case fuchsia::time::UtcSource::UNVERIFIED:
      case fuchsia::time::UtcSource::EXTERNAL:
        is_utc_time_accurate_ = true;
        utc_.Unbind();

        // Write the current difference between the UTC and monotonic clocks.
        if (const std::optional<zx::time_utc> current_utc_time = CurrentUtcTimeRaw(clock_);
            current_utc_time.has_value() && utc_monotonic_difference_file_.has_value()) {
          const zx::duration utc_monotonic_difference(current_utc_time.value().get() -
                                                      clock_->Now().get());
          files::WriteFile(utc_monotonic_difference_file_.value().CurrentBootPath(),
                           std::to_string(utc_monotonic_difference.get()));
        }

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
