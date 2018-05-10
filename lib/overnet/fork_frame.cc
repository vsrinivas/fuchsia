// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fork_frame.h"

namespace overnet {

Slice ForkFrame::Write() const {
  auto stream_id_length = stream_id_.wire_length();
  return introduction_.WithPrefix(stream_id_length + 1, [=](uint8_t* bytes) {
    uint8_t* p = bytes;
    p = stream_id_.Write(stream_id_length, p);
    *p++ = static_cast<uint8_t>(reliability_and_ordering_);
  });
}

StatusOr<ForkFrame> ForkFrame::Parse(Slice slice) {
  const uint8_t* begin = slice.begin();
  const uint8_t* p = begin;
  const uint8_t* end = slice.end();
  uint64_t stream_id;
  if (!varint::Read(&p, end, &stream_id)) {
    return StatusOr<ForkFrame>(StatusCode::DATA_LOSS,
                               "Failed to parse fork frame stream id");
  }
  if (p == end) {
    return StatusOr<ForkFrame>(
        StatusCode::DATA_LOSS,
        "Failed to parse fork frame reliability and ordering byte");
  }
  auto reliability_and_ordering = static_cast<ReliabilityAndOrdering>(*p++);
  return ForkFrame(StreamId(stream_id), reliability_and_ordering,
                   slice.FromOffset(p - begin));
}

}  // namespace overnet
