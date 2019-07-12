// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "probe_sequence.h"

#include <random>

namespace wlan {

ProbeSequence::ProbeTable ProbeSequence::RandomProbeTable() {
  std::random_device rd;
  std::mt19937 random_generator(rd());

  ProbeTable sequence_table;
  for (uint8_t i = 0; i < kNumProbeSequece; ++i) {
    for (tx_vec_idx_t j = kStartIdx; j <= kMaxValidIdx; ++j) {
      sequence_table[i][j - kStartIdx] = j;
    }
    std::shuffle(sequence_table[i].begin(), sequence_table[i].end(), random_generator);
  }
  return sequence_table;
}

bool ProbeSequence::Next(wlan::ProbeSequence::Entry* entry, tx_vec_idx_t* idx) const {
  bool ret = false;
  entry->probe_idx = (entry->probe_idx + 1) % kSequenceLength;
  if (entry->probe_idx == 0) {
    ret = true;
    entry->sequence_idx = (entry->sequence_idx + 1) % kNumProbeSequece;
  }
  *idx = probe_table[entry->sequence_idx][entry->probe_idx];
  return ret;
}

}  // namespace wlan
