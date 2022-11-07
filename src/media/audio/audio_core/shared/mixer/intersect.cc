// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/shared/mixer/intersect.h"

#include <lib/syslog/cpp/macros.h>

#include <algorithm>

namespace media::audio::mixer {

std::optional<Packet> IntersectPacket(const Format &format, const Packet &packet, Fixed range_start,
                                      int64_t range_length) {
  Fixed packet_start = packet.start;
  Fixed packet_end = packet_start + Fixed(packet.length);

  // Align the range to packet boundaries by shifting down.
  Fixed shift = range_start.Fraction() - packet_start.Fraction();
  if (shift < 0) {
    shift += Fixed(1);
  }

  range_start -= shift;
  Fixed range_end = range_start + Fixed(range_length);

  // Now intersect [packet_start, packet_end) with [range_start, range_end).
  Fixed isect_start = std::max(packet_start, range_start);
  Fixed isect_end = std::min(packet_end, range_end);
  Fixed isect_length = isect_end - isect_start;

  if (isect_length <= Fixed(0)) {
    return std::nullopt;
  }

  FX_CHECK(isect_length.Fraction() == Fixed(0))
      << ffl::String::DecRational << "packet_start=" << packet.start
      << ", packet_count=" << packet.length << ", range_start=" << Fixed(range_start + shift)
      << ", range_length=" << range_length << ", isect_start=" << isect_start
      << ", isect_end=" << isect_end;

  // Translate from frames to bytes.
  int64_t start_offset = Fixed(isect_start - packet_start).Floor();
  size_t payload_offset = static_cast<size_t>(start_offset) * format.bytes_per_frame();

  return Packet{
      .start = isect_start,
      .length = isect_length.Floor(),
      .payload = reinterpret_cast<uint8_t *>(packet.payload) + payload_offset,
  };
}

}  // namespace media::audio::mixer
