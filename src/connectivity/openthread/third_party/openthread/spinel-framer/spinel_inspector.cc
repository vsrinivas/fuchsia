// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "spinel_inspector.h"

#include <lib/zx/time.h>

namespace ot {

SpinelFramerInspector::SpinelFramerInspector() {
  history_[0] = inspect_.GetRoot().CreateChild("history_0");
  history_[1] = inspect_.GetRoot().CreateChild("history_1");
  history_[2] = inspect_.GetRoot().CreateChild("history_2");
  history_[3] = inspect_.GetRoot().CreateChild("history_3");
  history_[4] = inspect_.GetRoot().CreateChild("history_4");
  history_[5] = inspect_.GetRoot().CreateChild("history_5");
  for (size_t i = 0; i < kHistorySize; i++) {
    timestamp_[i] = history_[i].CreateUint("timestamp", 0);
    rcp_reset_count_[i] = history_[i].CreateUint("rcp_reset_count", 0);
    spi_frame_count_[i] = history_[i].CreateUint("spi_frame_count", 0);
    spi_valid_frame_count_[i] = history_[i].CreateUint("spi_valid_frame_count", 0);
    spi_garbage_frame_count_[i] = history_[i].CreateUint("spi_garbage_frame_count", 0);
    spi_duplex_frame_count_[i] = history_[i].CreateUint("spi_duplex_frame_count", 0);
    spi_unresponsive_frame_count_[i] = history_[i].CreateUint("spi_unresponsive_frame_count", 0);
    rx_frame_byte_count_[i] = history_[i].CreateUint("rx_frame_byte_count", 0);
    tx_frame_byte_count_[i] = history_[i].CreateUint("tx_frame_byte_count", 0);
    rx_frame_count_[i] = history_[i].CreateUint("rx_frame_rx_frame_count", 0);
    tx_frame_count_[i] = history_[i].CreateUint("tx_frame_count", 0);
  }
  idx_ = kHistorySize - 1;
}

::zx::vmo SpinelFramerInspector::DuplicateVmo() const { return inspect_.DuplicateVmo(); }

void SpinelFramerInspector::SetInspectData(
    uint64_t slave_reset_count, uint64_t spi_frame_count, uint64_t spi_valid_frame_count,
    uint64_t spi_garbage_frame_count, uint64_t spi_duplex_frame_count,
    uint64_t spi_unresponsive_frame_count, uint64_t rx_frame_byte_count,
    uint64_t tx_frame_byte_count, uint64_t rx_frame_count, uint64_t tx_frame_count) {
  rcp_reset_count_[idx_].Set(slave_reset_count);
  spi_frame_count_[idx_].Set(spi_frame_count);
  spi_valid_frame_count_[idx_].Set(spi_valid_frame_count);
  spi_garbage_frame_count_[idx_].Set(spi_garbage_frame_count);
  spi_duplex_frame_count_[idx_].Set(spi_duplex_frame_count);
  spi_unresponsive_frame_count_[idx_].Set(spi_unresponsive_frame_count);
  rx_frame_byte_count_[idx_].Set(rx_frame_byte_count);
  tx_frame_byte_count_[idx_].Set(tx_frame_byte_count);
  rx_frame_count_[idx_].Set(rx_frame_count);
  tx_frame_count_[idx_].Set(tx_frame_count);
}

void SpinelFramerInspector::UpdateIdx() {
  idx_ = (++idx_) % kHistorySize;
  timestamp_[idx_].Set(static_cast<uint64_t>(zx_clock_get_monotonic()));
  rcp_reset_count_[idx_].Set(0);
  spi_frame_count_[idx_].Set(0);
  spi_valid_frame_count_[idx_].Set(0);
  spi_garbage_frame_count_[idx_].Set(0);
  spi_duplex_frame_count_[idx_].Set(0);
  spi_unresponsive_frame_count_[idx_].Set(0);
  rx_frame_byte_count_[idx_].Set(0);
  tx_frame_byte_count_[idx_].Set(0);
  rx_frame_count_[idx_].Set(0);
  tx_frame_count_[idx_].Set(0);
}

}  // namespace ot
