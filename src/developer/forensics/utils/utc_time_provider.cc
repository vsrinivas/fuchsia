// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/utc_time_provider.h"

#include <lib/fit/function.h>

#include <optional>

#include "src/developer/forensics/utils/time.h"
#include "src/lib/files/file.h"

namespace forensics {

UtcTimeProvider::UtcTimeProvider(UtcClockReadyWatcher* utc_clock_ready_watcher,
                                 timekeeper::Clock* clock)
    : UtcTimeProvider(utc_clock_ready_watcher, clock, std::nullopt) {}

UtcTimeProvider::UtcTimeProvider(UtcClockReadyWatcher* utc_clock_ready_watcher,
                                 timekeeper::Clock* clock,
                                 PreviousBootFile utc_monotonic_difference_file)
    : UtcTimeProvider(utc_clock_ready_watcher, clock,
                      std::optional(utc_monotonic_difference_file)) {}

UtcTimeProvider::UtcTimeProvider(UtcClockReadyWatcher* utc_clock_ready_watcher,
                                 timekeeper::Clock* clock,
                                 std::optional<PreviousBootFile> utc_monotonic_difference_file)
    : clock_(clock),
      utc_monotonic_difference_file_(std::move(utc_monotonic_difference_file)),
      previous_boot_utc_monotonic_difference_(std::nullopt),
      utc_clock_ready_watcher_(utc_clock_ready_watcher) {
  utc_clock_ready_watcher->OnClockReady(fit::bind_member<&UtcTimeProvider::OnClockStart>(this));

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

std::optional<timekeeper::time_utc> UtcTimeProvider::CurrentTime() const {
  if (!utc_clock_ready_watcher_->IsUtcClockReady()) {
    return std::nullopt;
  }

  return CurrentUtcTimeRaw(clock_);
}

std::optional<zx::duration> UtcTimeProvider::CurrentUtcMonotonicDifference() const {
  if (!utc_clock_ready_watcher_->IsUtcClockReady()) {
    return std::nullopt;
  }

  if (const std::optional<timekeeper::time_utc> current_utc_time = CurrentUtcTimeRaw(clock_);
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

void UtcTimeProvider::OnClockStart() {
  // Write the current difference between the UTC and monotonic clocks.
  if (const std::optional<timekeeper::time_utc> current_utc_time = CurrentUtcTimeRaw(clock_);
      current_utc_time.has_value() && utc_monotonic_difference_file_.has_value()) {
    const zx::duration utc_monotonic_difference(current_utc_time.value().get() -
                                                clock_->Now().get());
    files::WriteFile(utc_monotonic_difference_file_.value().CurrentBootPath(),
                     std::to_string(utc_monotonic_difference.get()));
  }
}

}  // namespace forensics
