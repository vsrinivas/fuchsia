// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_WLANSOFTMAC_PROBE_SEQUENCE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_WLANSOFTMAC_PROBE_SEQUENCE_H_

#include <array>

#include <wlan/common/tx_vector.h>

namespace wlan {
class ProbeSequence {
 public:
  static constexpr uint8_t kNumProbeSequece = 8;
  static constexpr tx_vec_idx_t kSequenceLength = 1 + kMaxValidIdx - kStartIdx;
  using ProbeTable = std::array<std::array<tx_vec_idx_t, kSequenceLength>, kNumProbeSequece>;

  class Entry {
   private:
    friend class ProbeSequence;
    uint8_t sequence_idx = kNumProbeSequece - 1;
    tx_vec_idx_t probe_idx = kSequenceLength - 1;
  };

  static ProbeSequence RandomSequence() { return ProbeSequence(RandomProbeTable()); }
  ProbeSequence(ProbeTable&& table) : probe_table(std::move(table)) {}

  // returns true if probe_idx has wrapped around, which means we have probed
  // all tx vectors
  bool Next(Entry* entry, tx_vec_idx_t* idx) const;

 private:
  static ProbeTable RandomProbeTable();
  const ProbeTable probe_table;
};
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_WLANSOFTMAC_PROBE_SEQUENCE_H_
