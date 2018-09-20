// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/lib/overnet/labels/reliability_and_ordering.h"
#include "garnet/lib/overnet/labels/stream_id.h"
#include "garnet/lib/overnet/protocol/introduction.h"
#include "garnet/lib/overnet/vocabulary/slice.h"
#include "garnet/lib/overnet/vocabulary/status.h"

namespace overnet {

// Fork frames are used on the connection stream between nodes to establish new
// streams
class ForkFrame {
 public:
  ForkFrame(StreamId stream_id, ReliabilityAndOrdering reliability_and_ordering,
            Introduction introduction)
      : stream_id_(stream_id),
        reliability_and_ordering_(reliability_and_ordering),
        introduction_(std::move(introduction)) {}

  static StatusOr<ForkFrame> Parse(Slice slice);
  Slice Write(Border desired_border) const;

  StreamId stream_id() const { return stream_id_; }
  ReliabilityAndOrdering reliability_and_ordering() const {
    return reliability_and_ordering_;
  }
  const Introduction& introduction() const { return introduction_; }
  Introduction& introduction() { return introduction_; }

  friend bool operator==(const ForkFrame& a, const ForkFrame& b) {
    return std::tie(a.stream_id_, a.reliability_and_ordering_,
                    a.introduction_) ==
           std::tie(b.stream_id_, b.reliability_and_ordering_, b.introduction_);
  }

 private:
  StreamId stream_id_;
  ReliabilityAndOrdering reliability_and_ordering_;
  Introduction introduction_;
};

}  // namespace overnet
