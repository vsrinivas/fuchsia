// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_OPENTHREAD_THIRD_PARTY_OPENTHREAD_SPINEL_FRAMER_SPINEL_INSPECTOR_H_
#define SRC_CONNECTIVITY_OPENTHREAD_THIRD_PARTY_OPENTHREAD_SPINEL_FRAMER_SPINEL_INSPECTOR_H_

#include <lib/inspect/cpp/inspect.h>

#include <list>

namespace ot {
constexpr size_t kHistorySize = 6;

class SpinelFramerInspector {
 public:
  SpinelFramerInspector();
  void SetInspectData(uint64_t slave_reset_count, uint64_t spi_frame_count,
                      uint64_t spi_valid_frame_count, uint64_t spi_garbage_frame_count,
                      uint64_t spi_duplex_frame_count, uint64_t spi_unresponsive_frame_count,
                      uint64_t rx_frame_byte_count, uint64_t tx_frame_byte_count,
                      uint64_t rx_frame_count, uint64_t tx_frame_count);
  void UpdateIdx();
  ::zx::vmo DuplicateVmo() const;

 private:
  inspect::Inspector inspect_;
  inspect::Node history_[kHistorySize];
  inspect::UintProperty timestamp_[kHistorySize];
  inspect::UintProperty rcp_reset_count_[kHistorySize];
  inspect::UintProperty spi_frame_count_[kHistorySize];
  inspect::UintProperty spi_valid_frame_count_[kHistorySize];
  inspect::UintProperty spi_garbage_frame_count_[kHistorySize];
  inspect::UintProperty spi_duplex_frame_count_[kHistorySize];
  inspect::UintProperty spi_unresponsive_frame_count_[kHistorySize];
  inspect::UintProperty rx_frame_byte_count_[kHistorySize];
  inspect::UintProperty tx_frame_byte_count_[kHistorySize];
  inspect::UintProperty rx_frame_count_[kHistorySize];
  inspect::UintProperty tx_frame_count_[kHistorySize];
  size_t idx_;
};

}  // namespace ot

#endif  // SRC_CONNECTIVITY_OPENTHREAD_THIRD_PARTY_OPENTHREAD_SPINEL_FRAMER_SPINEL_INSPECTOR_H_
