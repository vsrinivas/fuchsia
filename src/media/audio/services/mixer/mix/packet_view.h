// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_PACKET_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_PACKET_H_

#include <ostream>

#include "src/media/audio/services/mixer/common/basic_types.h"

namespace media_audio {

// Represents a view to a fixed-sized packet of audio data.
class PacketView {
 public:
  struct Args {
    Format format;   // format of audio frames in this packet
    Fixed start;     // starting position of the packet
    int64_t length;  // number of frames in the packet; must be > 0
    void* payload;   // payload buffer
  };
  explicit PacketView(Args args);

  // Reports the format of audio frames in this packet.
  const Format& format() const { return format_; }

  // Reports the position of the packet's first frame.
  Fixed start() const { return start_; }

  // Reports the position just after the packet's last frame.
  Fixed end() const { return end_; }

  // Reports the number of frames in this packet.
  int64_t length() const { return length_; }

  // Returns the payload of this packet.
  void* payload() const { return payload_; }

  // Extracts a slice of this packet.
  // REQUIRES: 0 <= start_offset < end_offset <= length
  PacketView Slice(int64_t start_offset, int64_t end_offset) const;

  // Intersects this packet with the given range, returning a packet that overlaps the given range,
  // or std::nullopt if there is no overlap. The intersection is guaranteed to start and end on a
  // frame boundary and the intersection is never larger than the packet or the range.
  // That is, for every non-nullopt result:
  //
  //   result.start = this.start + k*frame_size, for some non-negative integer k, and
  //   length <= min(packet.length, range_length)
  //
  // For example:
  //
  //   IntersectionWith(this = {.start = 0.0, .length = 10},
  //                    range_start = 1,
  //                    range_length = 2)
  //
  //   returns:
  //     .start = 1.0
  //     .length = 2
  //     .payload = packet.payload + 1 frame
  //
  // When the range starts or ends on a fractional frame, the intersection is shifted to include
  // complete frames. The intersection starts with the first frame in the packet that overlaps the
  // range. For example:
  //
  //   IntersectionWith(this = {.start = 0.0, .length = 10},
  //                    range_start = 1.5,
  //                    range_length = 2);
  //
  //   returns:
  //     .start = 1.0
  //     .length = 2
  //     .payload = packet.payload + 1 frame
  //
  // The packet may start on a fractional frame position. For example:
  //
  //   IntersectionWith(this = {.start = 0.9, .length = 10},
  //                    range_start = 2.5,
  //                    range_length = 3);
  //
  //   returns:
  //     .start = 1.9
  //     .length = 3
  //     .payload = packet.payload + 1 frame
  std::optional<PacketView> IntersectionWith(Fixed range_start, int64_t range_length) const;

 private:
  Format format_;
  Fixed start_;
  Fixed end_;
  int64_t length_;
  void* payload_;
};

// Convenience for logging.
std::ostream& operator<<(std::ostream& out, const PacketView& packet);

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_PACKET_H_
