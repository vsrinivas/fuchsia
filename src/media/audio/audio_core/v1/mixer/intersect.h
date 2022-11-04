// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_MIXER_INTERSECT_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_MIXER_INTERSECT_H_

#include <optional>

#include "src/media/audio/lib/format/constants.h"
#include "src/media/audio/lib/format/format.h"

namespace media::audio::mixer {

struct Packet {
  Fixed start;     // frame position of the first frame in payload
  int64_t length;  // number of frames in payload
  void* payload;   // payload buffer
};

// Returns the frames in packet which overlap the given range, or returns std::nullopt if there is
// no overlap. The intersection is guaranteed to start and end on a frame boundary. That is, for
// every non-nullopt result, start = packet.start*k for some non-negative integer k.
//
// The intersection is never larger than the packet or the range. That is, for every non-nullopt
// result, length <= min(packet.length, range_length). For example:
//
//   IntersectPacket(packet = {.start = 0.0, .length = 10},
//                   range_start = 1,
//                   range_length = 2);
//
//   returns:
//     .start = 1.0
//     .length = 2
//     .payload = packet.payload + 1 frame
//
// When the range starts or ends on a fractional frame, the intersection is shifted to include
// complete frames. The intersection starts with the packet's first frame that overlaps the range.
// For example:
//
//   IntersectPacket(packet = {.start = 0.0, .length = 10},
//                   range_start = 1.5,
//                   range_length = 2);
//
//   returns:
//     .start = 1.0
//     .length = 2
//     .payload = packet.payload + 1 frame
//
// The packet may start on a fractional frame position. For example:
//
//   IntersectPacket(packet = {.start = 0.9, .length = 10},
//                   range_start = 2.5,
//                   range_length = 3);
//
//   returns:
//     .start = 1.9
//     .length = 3
//     .payload = packet.payload + 1 frame
std::optional<Packet> IntersectPacket(const Format& format, const Packet& packet, Fixed range_start,
                                      int64_t range_length);

}  // namespace media::audio::mixer

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_MIXER_INTERSECT_H_
