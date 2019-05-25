// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "src/connectivity/overnet/lib/vocabulary/optional.h"
#include "src/connectivity/overnet/lib/vocabulary/slice.h"
#include "src/connectivity/overnet/lib/vocabulary/status.h"

namespace overnet {

// Manages the framing and unframing of packets in a stream
class StreamFramer {
 public:
  StreamFramer(Border desired_border, uint32_t maximum_segment_size)
      : desired_border(desired_border),
        maximum_segment_size(maximum_segment_size) {}
  virtual ~StreamFramer() = default;

  const Border desired_border;
  const uint32_t maximum_segment_size;

  // Input loop:
  //   incoming_data = read_from_stream();
  //   Push(incoming_data);
  //   while (auto frame = Pop()) {
  //     process_frame(*frame);
  //   }
  virtual void Push(Slice data) = 0;
  virtual StatusOr<Optional<Slice>> Pop() = 0;
  // Returns true if nothing is buffered.
  virtual bool InputEmpty() const = 0;
  // Skip content if stuck.
  // Should be called after some appropriate timeout (some small multiple of the
  // time required to transmit MaximumSegmentSize()).
  // Allows the framer to skip noise reliably.
  // Returns what was skipped (or Nothing if a skip is unavailable).
  virtual Optional<Slice> SkipNoise() = 0;

  // Output loop:
  //   frame = construct_frame(MaximumSegmentSize());
  //   outgoing_data = Frame(frame);
  //   write(*outgoing_data);
  virtual Slice Frame(Slice data) = 0;
};

}  // namespace overnet
