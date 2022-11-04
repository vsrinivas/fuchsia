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
      start_frame_(args.start_frame),
      end_frame_(args.start_frame + Fixed(args.frame_count)),
      frame_count_(args.frame_count),
      payload_(args.payload) {
  FX_CHECK(args.frame_count > 0) << "packet frame_count '" << args.frame_count
                                 << "' must be positive";
}

PacketView PacketView::Slice(int64_t start_offset, int64_t end_offset) const {
  FX_CHECK(0 <= start_offset && start_offset < end_offset && end_offset <= frame_count())
      << "Invalid slice [" << start_offset << ", " << end_offset << ") of " << *this;

  auto byte_offset = static_cast<size_t>(start_offset * format().bytes_per_frame());

  return PacketView({.format = format(),
                     .start_frame = start_frame() + Fixed(start_offset),
                     .frame_count = end_offset - start_offset,
                     .payload = reinterpret_cast<uint8_t*>(payload()) + byte_offset});
}

std::optional<PacketView> PacketView::IntersectionWith(Fixed range_start_frame,
                                                       int64_t range_frame_count) const {
  // Align the range to frame boundaries by shifting down.
  Fixed shift = range_start_frame.Fraction() - start_frame().Fraction();
  if (shift < 0) {
    shift += Fixed(1);
  }

  range_start_frame -= shift;
  const Fixed range_end_frame = range_start_frame + Fixed(range_frame_count);

  // Now intersect [start_frame(), end_frame()) with [start_frame, range_end_frame).
  const Fixed isect_offset_start = std::max(start_frame(), range_start_frame) - start_frame();
  const Fixed isect_offset_end = std::min(end_frame(), range_end_frame) - start_frame();

  // Offsets must be integral.
  FX_CHECK(isect_offset_start.Fraction() == Fixed(0) && isect_offset_end.Fraction() == Fixed(0))
      << ffl::String::DecRational << "packet=" << *this << ","
      << " range=[" << range_start_frame << ", " << range_end_frame << "),"
      << " isect_offset=[" << isect_offset_start << ", " << isect_offset_end << ")";

  const auto start_offset = isect_offset_start.Floor();
  const auto end_offset = isect_offset_end.Floor();
  if (end_offset <= start_offset) {
    return std::nullopt;
  }
  return Slice(start_offset, end_offset);
}

std::ostream& operator<<(std::ostream& out, const PacketView& packet) {
  out << ffl::String::DecRational << "[" << packet.start_frame() << ", " << packet.end_frame()
      << ")";
  return out;
}

}  // namespace media_audio
