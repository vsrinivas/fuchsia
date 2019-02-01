// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_METRICS_RATE_TRACKER_H_
#define GARNET_BIN_MEDIAPLAYER_METRICS_RATE_TRACKER_H_

#include "garnet/bin/mediaplayer/metrics/value_tracker.h"

namespace media_player {

// Tracks the rate at which an event occurs.
class RateTracker {
 public:
  RateTracker();

  ~RateTracker();

  // Adds a sample to the tracker.
  void AddSample(int64_t now, bool progressing);

  // Resets the tracker to its initial state.
  void Reset();

  // Rate of progress samples.
  double progress_samples_per_second() const {
    return kNsPerSecond / average_progress_interval();
  };

  size_t progress_interval_count() const { return progress_intervals_.count(); }

  // Minimum inter-sample interval when progressing.
  int64_t min_progress_interval() const { return progress_intervals_.min(); }

  // Average inter-sample interval when progressing.
  int64_t average_progress_interval() const {
    return progress_intervals_.average();
  }

  // Maximum inter-sample interval when progressing.
  int64_t max_progress_interval() const { return progress_intervals_.max(); }

 private:
  static constexpr double kNsPerSecond = 1'000'000'000.0;
  int64_t last_progressing_sample_time_;
  ValueTracker<int64_t> progress_intervals_;
};

std::ostream& operator<<(std::ostream& os, const RateTracker& value);

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_METRICS_RATE_TRACKER_H_
