// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/metrics/packet_timing_tracker.h"

#include "garnet/bin/mediaplayer/framework/formatting.h"

namespace media_player {

PacketTimingTracker::PacketTimingTracker(bool count_late_samples)
    : count_late_samples_(count_late_samples) {
  Reset();
}

PacketTimingTracker::~PacketTimingTracker() {}

void PacketTimingTracker::AddSample(int64_t now, int64_t presentation_time,
                                    int64_t packet_pts_ns, bool progressing) {
  if (!progressing) {
    ++not_progressing_count_;
  } else if (packet_pts_ns == Packet::kUnknownPts) {
    ++no_packet_count_;
  } else {
    int64_t earliness = packet_pts_ns - presentation_time;

    earliness_.AddSample(earliness);

    if (earliness < 0) {
      ++late_count_;
    }
  }
}

void PacketTimingTracker::Reset() {
  earliness_.Reset();
  not_progressing_count_ = 0;
  late_count_ = 0;
}

std::ostream& operator<<(std::ostream& os, const PacketTimingTracker& value) {
  os << fostr::NewLine << "nominal           " << value.nominal_count();

  if (value.late_count() != 0) {
    os << fostr::NewLine << "late              " << value.late_count();
  }

  if (value.no_packet_count() != 0) {
    os << fostr::NewLine << "no packet         " << value.no_packet_count();
  }

  os << fostr::NewLine << "not progressing   " << value.not_progressing_count();
  os << fostr::NewLine << "total             " << value.count();

  if (value.nominal_count()) {
    os << fostr::NewLine << "presentation offset:";
    os << fostr::NewLine << "    minimum       " << AsNs(value.min_earliness());
    os << fostr::NewLine << "    average       "
       << AsNs(value.average_earliness());
    os << fostr::NewLine << "    maximum       " << AsNs(value.max_earliness());
  }

  return os;
}

}  // namespace media_player
