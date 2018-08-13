// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_METRICS_PACKET_TIMING_TRACKER_H_
#define GARNET_BIN_MEDIAPLAYER_METRICS_PACKET_TIMING_TRACKER_H_

#include "garnet/bin/mediaplayer/metrics/value_tracker.h"

namespace media_player {

// Tracks packet timing information.
class PacketTimingTracker {
 public:
  PacketTimingTracker(bool count_late_samples);

  ~PacketTimingTracker();

  // Adds a sample to the tracker. If |packet_pts_ns| is |Packet::kUnknownPts|,
  // the sample is counted as a 'no packet' case.
  void AddSample(int64_t now, int64_t presentation_time, int64_t packet_pts_ns,
                 bool progressing);

  // Resets the tracker to its initial state.
  void Reset();

  // Sample count (nominal, late, no packet and not progressing).
  size_t count() const {
    return earliness_.count() + no_packet_count_ + not_progressing_count_;
  }

  // Nominal (progressing, not late) sample count.
  size_t nominal_count() const { return earliness_.count() - late_count(); }

  // Count of samples for which the timeline wasn't progressing.
  size_t not_progressing_count() const { return not_progressing_count_; }

  // Count of packets passing when the presentation time was greater than the
  // packet PTS.
  size_t late_count() const { return count_late_samples_ ? late_count_ : 0; }

  // Count of samples with |packet_pts_ns| equal to |Packet::kUnknownPts|.
  size_t no_packet_count() const { return no_packet_count_; }

  // Minimum of packet PTS minus presentation time.
  int64_t min_earliness() const { return earliness_.min(); }

  // Average of packet PTS minus presentation time.
  int64_t average_earliness() const { return earliness_.average(); }

  // Maximum of packet PTS minus presentation time.
  int64_t max_earliness() const { return earliness_.max(); }

 private:
  bool count_late_samples_;
  ValueTracker<int64_t> earliness_;
  size_t not_progressing_count_;
  size_t late_count_;
  size_t no_packet_count_;
};

std::ostream& operator<<(std::ostream& os, const PacketTimingTracker& value);

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_METRICS_PACKET_TIMING_TRACKER_H_
