// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_UTILS_UTC_TIME_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_UTILS_UTC_TIME_PROVIDER_H_

#include <fuchsia/time/cpp/fidl.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>
#include <optional>
#include <string>

#include "src/developer/forensics/utils/previous_boot_file.h"
#include "src/lib/timekeeper/clock.h"
#include "src/lib/timekeeper/system_clock.h"

namespace forensics {

// Provides the UTC time only if the device's system clock is accurate.
//
// Can be configured to record the UTC-monotonic difference from the previous boot by providing a
// non-nullopt |utc_monotonic_difference_path|.
class UtcTimeProvider {
 public:
  // fuchsia.time.Utc is expected to be in |services|.
  UtcTimeProvider(std::shared_ptr<sys::ServiceDirectory> services, timekeeper::Clock* clock);
  UtcTimeProvider(std::shared_ptr<sys::ServiceDirectory> services, timekeeper::Clock* clock,
                  PreviousBootFile utc_monotonic_difference_file);

  // Returns the current UTC time if the device's UTC time is accurate, std::nullopt otherwise.
  std::optional<zx::time_utc> CurrentTime() const;

  // Returns the difference between the UTC clock and the device's monotonic time if the device's
  // UTC time is accurate, std::nullopt otherwise.
  //
  // This value can be added to a monotonic time to convert it to a UTC time.
  std::optional<zx::duration> CurrentUtcMonotonicDifference() const;
  std::optional<zx::duration> PreviousBootUtcMonotonicDifference() const;

 private:
  UtcTimeProvider(std::shared_ptr<sys::ServiceDirectory> services, timekeeper::Clock* clock,
                  std::optional<PreviousBootFile> utc_monotonic_difference_file);

  // Keeps making asynchronous calls until the UTC time is accurate.
  void WatchForAccurateUtcTime();

  std::shared_ptr<sys::ServiceDirectory> services_;
  timekeeper::Clock* clock_;

  fuchsia::time::UtcPtr utc_;

  std::optional<PreviousBootFile> utc_monotonic_difference_file_;

  // The last difference between the UTC and monotonic clocks in the previous boot.
  std::optional<zx::duration> previous_boot_utc_monotonic_difference_;

  bool is_utc_time_accurate_ = false;
};

}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_UTILS_UTC_TIME_PROVIDER_H_
