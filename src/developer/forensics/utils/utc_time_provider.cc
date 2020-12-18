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

UtcTimeProvider::UtcTimeProvider(async_dispatcher_t* dispatcher, zx::unowned_clock clock_handle,
                                 timekeeper::Clock* clock)
    : UtcTimeProvider(dispatcher, std::move(clock_handle), clock, std::nullopt) {}

UtcTimeProvider::UtcTimeProvider(async_dispatcher_t* dispatcher, zx::unowned_clock clock_handle,
                                 timekeeper::Clock* clock,
                                 PreviousBootFile utc_monotonic_difference_file)
    : UtcTimeProvider(dispatcher, std::move(clock_handle), clock,
                      std::optional(utc_monotonic_difference_file)) {}

UtcTimeProvider::UtcTimeProvider(async_dispatcher_t* dispatcher, zx::unowned_clock clock_handle,
                                 timekeeper::Clock* clock,
                                 std::optional<PreviousBootFile> utc_monotonic_difference_file)
    : clock_(clock),
      utc_monotonic_difference_file_(std::move(utc_monotonic_difference_file)),
      previous_boot_utc_monotonic_difference_(std::nullopt),
      wait_for_clock_start_(this, clock_handle->get_handle(), ZX_CLOCK_STARTED, /*options=*/0) {
  if (const zx_status_t status = wait_for_clock_start_.Begin(dispatcher); status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "Failed to wait for clock start";
  }

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

void UtcTimeProvider::OnClockStart(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                   zx_status_t status, const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    FX_PLOGS(WARNING, status) << "Wait for clock start completed with error, trying again";
    
    // Attempt to wait for the clock to start again.
    wait->Begin(dispatcher);
    return;
  }

  is_utc_time_accurate_ = true;

  // Write the current difference between the UTC and monotonic clocks.
  if (const std::optional<zx::time_utc> current_utc_time = CurrentUtcTimeRaw(clock_);
      current_utc_time.has_value() && utc_monotonic_difference_file_.has_value()) {
    const zx::duration utc_monotonic_difference(current_utc_time.value().get() -
                                                clock_->Now().get());
    files::WriteFile(utc_monotonic_difference_file_.value().CurrentBootPath(),
                     std::to_string(utc_monotonic_difference.get()));
  }
}

}  // namespace forensics
