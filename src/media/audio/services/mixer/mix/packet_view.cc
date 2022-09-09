// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/packet_view.h"

#include <lib/syslog/cpp/macros.h>

#include <ffl/string.h>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/services/common/logging.h"

namespace media_audio {

PacketView::PacketView(Args args)
    : format_(args.format),
      start_(args.start),
      end_(args.start + Fixed(args.length)),
      length_(args.length),
      payload_(args.payload) {
  FX_CHECK(args.length > 0) << "packet length '" << args.length << "' must be positive";
}

PacketView PacketView::Slice(int64_t start_offset, int64_t end_offset) const {
  FX_CHECK(0 <= start_offset && start_offset < end_offset && end_offset <= length())
      << "Invalid slice [" << start_offset << ", " << end_offset << ") of " << *this;

  auto byte_offset = static_cast<size_t>(start_offset * format().bytes_per_frame());

  return PacketView({.format = format(),
                     .start = start() + Fixed(start_offset),
                     .length = end_offset - start_offset,
                     .payload = reinterpret_cast<uint8_t*>(payload()) + byte_offset});
}

std::optional<PacketView> PacketView::IntersectionWith(Fixed range_start,
                                                       int64_t range_length) const {
  // Align the range to frame boundaries by shifting down.
  Fixed shift = range_start.Fraction() - start().Fraction();
  if (shift < 0) {
    shift += Fixed(1);
  }

  range_start -= shift;
  Fixed range_end = range_start + Fixed(range_length);

  // Now intersect [start(), end()) with [range_start, range_end).
  Fixed isect_offset_start = std::max(start(), range_start) - start();
  Fixed isect_offset_end = std::min(end(), range_end) - start();

  // Offsets must be integral.
  FX_CHECK(isect_offset_start.Fraction() == Fixed(0) && isect_offset_end.Fraction() == Fixed(0))
      << ffl::String::DecRational << "packet=" << *this << ","
      << " range=[" << range_start << ", " << range_end << "),"
      << " isect_offset=[" << isect_offset_start << ", " << isect_offset_end << ")";

  auto start_offset = isect_offset_start.Floor();
  auto end_offset = isect_offset_end.Floor();
  if (end_offset <= start_offset) {
    return std::nullopt;
  }
  return Slice(start_offset, end_offset);
}

std::ostream& operator<<(std::ostream& out, const PacketView& packet) {
  out << ffl::String::DecRational << "[" << packet.start() << ", " << packet.end() << ")";
  return out;
}

}  // namespace media_audio
