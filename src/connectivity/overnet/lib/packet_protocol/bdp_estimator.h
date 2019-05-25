// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>

#include "src/connectivity/overnet/lib/packet_protocol/windowed_filter.h"

namespace overnet {

class BdpEstimator {
 public:
  BdpEstimator() : filter_(1024, 0, 0) {}

  struct PerPacketData {
    uint64_t seq;
    uint64_t bytes_received_at_send;
  };

  void ReceivedBytes(uint64_t count) { bytes_received_ += count; }

  PerPacketData SentPacket(uint64_t seq) {
    return PerPacketData{seq, bytes_received_};
  }

  void AckPacket(PerPacketData data) {
    filter_.Update(data.seq, bytes_received_ - data.bytes_received_at_send);
  }

  uint64_t estimate() const { return filter_.best_estimate(); }

 private:
  uint64_t bytes_received_ = 0;
  WindowedFilter<uint64_t, uint64_t, MaxFilter> filter_;
};

}  // namespace overnet
