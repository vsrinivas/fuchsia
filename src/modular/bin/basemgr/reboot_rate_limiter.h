// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_BASEMGR_REBOOT_RATE_LIMITER_H_
#define SRC_MODULAR_BIN_BASEMGR_REBOOT_RATE_LIMITER_H_

#include <lib/zx/status.h>

#include <chrono>
#include <string>

namespace modular {

// |RebootRateLimiter| is a helper class for rate limiting reboot attempts.
//
// In order to mitigate rapid boot loops, as triggered by certain parts of
// component, this class helps implement exponential backoff for reboot attempts.
// It does so by storing necessary information in a persistent file. Note that
// this class is strictly a helper class. It does not trigger the reboot. Instead,
// such work is delegated to clients. Instead, clients of this class should use
// this to determine if they should reboot. The rough flow is this:
//
//
//    auto reboot_rate_limiter = RebootRateLimiter(...);
//    if (reboot_rate_limiter.CanReboot()) {
//        reboot_rate_limiter.UpdateTrackingFile();
//        // Trigger reboot.
//    }
//
class RebootRateLimiter {
 public:
  using SystemClock = std::chrono::system_clock;
  using Duration = SystemClock::duration;
  using TimePoint = SystemClock::time_point;

  // Target constructor.
  //
  // |tracking_file_path| is a filepath used to store/retrieve reboot tracking
  // data. It is expected that clients use the same path at all times.
  //
  // |backoff_base| is the base number, in minutes, used to calculate
  // exponential backoff delay. The idea here is that the delay, in minutes,
  // would equal |backoff_base| ^ attempt, where attempt is the number of
  // attempts listed in the file located at |tracking_file_path|.
  //
  // |max_delay| is the maximum number that the exponential backoff delay will
  // go to. This is used to cap the wait time at a reasonable limit.
  //
  // |tracking_file_ttl| refers to the Time To Live (TTL) for the tracking file
  // passed at |tracking_file_path|. After this time period, the tracking file
  // will be reset to 0.
  explicit RebootRateLimiter(std::string tracking_file_path,
                             size_t backoff_base = kBackoffBaseInMinutes,
                             size_t max_delay = kMaxDelayInMinutes,
                             Duration tracking_file_ttl = std::chrono::hours(24));

  // Determines if the device is safe to reboot.
  //
  // |timepoint| is the reference time to measure the last reboot time against.
  //
  // Returns true if any of the following conditions are met:
  //  * Sufficient time has past since last reboot, using |backoff_base|
  //    and counter in tracking file to determine this.
  //  * Tracking file is does not exist. This is expected for the first usage
  //    of this class on a new file path.
  // Returns false otherwise.
  //
  // Note that if the elapsed time since the last reboot is greater than
  // |tracking_file_ttl|, this function will reset the tracking file.
  zx::result<bool> CanReboot(TimePoint timepoint = SystemClock::now());

  // Updates the file at |tracking_file_path| to contain the time passed in
  // via |timepoint|, and the reboot counter incremented by 1. This function
  // will create the file if it is not present, setting the counter to 1.
  zx::result<> UpdateTrackingFile(TimePoint timepoint = SystemClock::now());

 private:
  // Base number used for calculating exponential backoff delay. The idea here
  // is that the delay, in minutes, would equal kBackoffBaseInMinutes ^ attempt.
  static constexpr size_t kBackoffBaseInMinutes = 2;

  // Default value for max delay used in exponential backoff.
  static constexpr size_t kMaxDelayInMinutes = 64;

  // Default value for Time-to-live (TTL) for tracking file.
  static constexpr Duration kTrackingFileTTL = std::chrono::hours(24);

  // Format for the timestamp of the last reboot time as stored by |CanReboot|.
  // Format is "YYYY-MM-DD HH:MM:SS"
  // For reference, see: http://www.cplusplus.com/reference/ctime/strftime/.
  static constexpr char kTimestampFormat[] = "%F %T";

  static std::string SerializeLastReboot(TimePoint timepoint, size_t reboots);
  static zx::result<std::pair<TimePoint, size_t>> DeserializeLastReboot(std::string_view payload);

  std::string tracking_file_path_;
  size_t backoff_base_;
  size_t max_delay_;
  Duration tracking_file_ttl_;
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_BASEMGR_REBOOT_RATE_LIMITER_H_
