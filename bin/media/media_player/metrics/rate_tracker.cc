// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_player/metrics/rate_tracker.h"

#include "garnet/bin/media/media_player/framework/formatting.h"
#include "garnet/bin/media/media_player/framework/packet.h"

namespace media_player {

RateTracker::RateTracker() { Reset(); }

RateTracker::~RateTracker() {}

void RateTracker::Reset() {
  last_progressing_sample_time_ = Packet::kUnknownPts;
  progress_intervals_.Reset();
}

void RateTracker::AddSample(int64_t now, bool progressing) {
  if (!progressing) {
    last_progressing_sample_time_ = Packet::kUnknownPts;
  } else {
    if (last_progressing_sample_time_ != Packet::kUnknownPts) {
      progress_intervals_.AddSample(now - last_progressing_sample_time_);
    }

    last_progressing_sample_time_ = now;
  }
}

std::ostream& operator<<(std::ostream& os, const RateTracker& value) {
  os << fostr::NewLine << "rate per second   "
     << value.progress_samples_per_second();
  os << fostr::NewLine << "minimum interval  "
     << AsNs(value.min_progress_interval());
  os << fostr::NewLine << "average interval  "
     << AsNs(value.average_progress_interval());
  return os << fostr::NewLine << "maximum interval  "
            << AsNs(value.max_progress_interval());
}

}  // namespace media_player
