// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_UTILS_UTC_TIME_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_UTILS_UTC_TIME_PROVIDER_H_

#include <optional>

#include "src/developer/forensics/utils/previous_boot_file.h"
#include "src/developer/forensics/utils/utc_clock_ready_watcher.h"
#include "src/lib/timekeeper/clock.h"

namespace forensics {

// Provides the UTC time only if the device's UTC clock has started.
//
// Can be configured to record the UTC-monotonic difference from the previous boot by providing a
// non-nullopt |utc_monotonic_difference_path|.
class UtcTimeProvider {
 public:
  UtcTimeProvider(UtcClockReadyWatcher* utc_clock_ready_watcher, timekeeper::Clock* clock);
  UtcTimeProvider(UtcClockReadyWatcher* utc_clock_ready_watcher, timekeeper::Clock* clock,
                  PreviousBootFile utc_monotonic_difference_file);

  // Returns the current UTC time if the device's UTC time is accurate, std::nullopt otherwise.
  std::optional<timekeeper::time_utc> CurrentTime() const;

  // Returns the difference between the UTC clock and the device's monotonic time if the device's
  // UTC time is accurate, std::nullopt otherwise.
  //
  // This value can be added to a monotonic time to convert it to a UTC time.
  std::optional<zx::duration> CurrentUtcMonotonicDifference() const;
  std::optional<zx::duration> PreviousBootUtcMonotonicDifference() const;

 private:
  UtcTimeProvider(UtcClockReadyWatcher* utc_clock_ready_watcher, timekeeper::Clock* clock,
                  std::optional<PreviousBootFile> utc_monotonic_difference_file);

  // Keep waiting on the clock handle until the clock has started.
  void OnClockStart();

  timekeeper::Clock* clock_;

  std::optional<PreviousBootFile> utc_monotonic_difference_file_;

  // The last difference between the UTC and monotonic clocks in the previous boot.
  std::optional<zx::duration> previous_boot_utc_monotonic_difference_;

  UtcClockReadyWatcher* utc_clock_ready_watcher_;
};

}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_UTILS_UTC_TIME_PROVIDER_H_
