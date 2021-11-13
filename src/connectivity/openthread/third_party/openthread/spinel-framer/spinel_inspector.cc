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
    timestamp_ms_[i] = history_[i].CreateUint("last_reset_timestamp_ms", 0);
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
  data_set_timestamp_ms_ = 0;
}

int64_t SpinelFramerInspector::GetTimeMs() {
  return zx_clock_get_monotonic() / kNanoSecondsPerMilliSeconds;
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
  data_set_timestamp_ms_ = GetTimeMs();
}

void SpinelFramerInspector::UpdateIdx() {
  idx_ = (++idx_) % kHistorySize;
  timestamp_ms_[idx_].Set(GetTimeMs());
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

int SpinelFramerInspector::GetInspectRemainingTimeMilliSeconds() {
  int64_t now_ms = GetTimeMs();
  int64_t expected_inspect_time_ms = kInspectIntervalMilliSeconds + data_set_timestamp_ms_;
  // this works since the remaining time is relatively short
  return static_cast<int>(
      (expected_inspect_time_ms <= now_ms) ? 0 : (expected_inspect_time_ms - now_ms));
}

}  // namespace ot
