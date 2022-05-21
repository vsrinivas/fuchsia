// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/clock/logging.h"

#include <lib/syslog/cpp/macros.h>

#include <atomic>
#include <iomanip>

namespace media_audio {

namespace {
// Whether to enable LogClockAdjustment. If false, then LogClockAdjustment is a no-op.
constexpr bool kLogClockAdjustment = true;
// Within LogClockAdjustment, whether to include PID coefficients in the log.
constexpr bool kLogClockAdjustmentWithPidCoefficients = false;
// Within LogCLockAdjustment, log every kLogClockAdjustmentStride or if the position error
// exceeds kLogClockAdjustmentPositionErrorThreshold.
constexpr int64_t kLogClockAdjustmentStride = 97;  // make strides prime, to avoid periodicity
constexpr zx::duration kLogClockAdjustmentPositionErrorThreshold = zx::nsec(500);
}  // namespace

void LogClockAdjustment(const Clock& clock, std::optional<int32_t> last_rate_ppm,
                        int32_t next_rate_ppm, zx::duration pos_error,
                        const ::media::audio::clock::PidControl& pid) {
  if constexpr (!kLogClockAdjustment) {
    return;
  }

  static std::atomic<int64_t> log_count(0);

  // If absolute error is large enough, then log now but reset our stride.
  bool always_log = false;
  if (std::abs(pos_error.to_nsecs()) >= kLogClockAdjustmentPositionErrorThreshold.to_nsecs()) {
    log_count.store(1);
    always_log = true;
  }

  if (always_log || log_count.fetch_add(1) % kLogClockAdjustmentStride == 0) {
    std::stringstream os;
    os << (&clock) << " " << clock.name();
    if (!last_rate_ppm) {
      os << " set to (ppm)              " << std::setw(4) << next_rate_ppm;
    } else if (next_rate_ppm != *last_rate_ppm) {
      os << " change from (ppm) " << std::setw(4) << *last_rate_ppm << " to " << std::setw(4)
         << next_rate_ppm;
    } else {
      os << " adjust_ppm remains  (ppm) " << std::setw(4) << *last_rate_ppm;
      if constexpr (kLogClockAdjustmentWithPidCoefficients) {
        os << "; PID " << pid;
      }
    }
    os << "; src_pos_err " << pos_error.to_nsecs() << " ns";
    FX_LOGS(INFO) << os.str();
  }
}

}  // namespace media_audio
