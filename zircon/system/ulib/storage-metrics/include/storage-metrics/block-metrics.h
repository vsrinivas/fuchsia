// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef STORAGE_METRICS_BLOCK_METRICS_H_
#define STORAGE_METRICS_BLOCK_METRICS_H_

#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/hardware/block/llcpp/fidl.h>
#include <lib/zx/time.h>
#include <stdio.h>

#include <atomic>

#include "storage-metrics.h"

namespace storage_metrics {

using BlockStatFidl = ::llcpp::fuchsia::hardware::block::BlockStats;

// Compares block stat for read, write, trim, flush, barrier_before and
// barrier_after. Returns false if the stats dont match.
bool BlockStatEqual(const BlockStatFidl& lhs, const BlockStatFidl& rhs);

class BlockDeviceMetrics : public storage_metrics::Metrics {
 public:
  BlockDeviceMetrics() = default;
  explicit BlockDeviceMetrics(const BlockStatFidl* metrics);
  explicit BlockDeviceMetrics(const fuchsia_hardware_block_BlockStats* metrics);
  BlockDeviceMetrics(const BlockDeviceMetrics&) = delete;
  BlockDeviceMetrics(BlockDeviceMetrics&& rhs) = delete;
  BlockDeviceMetrics& operator=(const BlockDeviceMetrics&) = delete;
  BlockDeviceMetrics& operator=(BlockDeviceMetrics&&) = delete;
  ~BlockDeviceMetrics() = default;

  // Copies to fields of fidl structure the corresponding fields of BlockDeviceMetrics
  void CopyToFidl(BlockStatFidl* metrics) const;

  // Copies to fields of fidl structure the corresponding fields of BlockDeviceMetrics
  void CopyToFidl(fuchsia_hardware_block_BlockStats* metrics) const;

  // Prints the fields of BlockDeviceMetrics to file |stream|.
  void Dump(FILE* stream, std::optional<bool> success = std::nullopt) const;

  // Following Update*Stat functions take |success|, which denotes whether the
  // call was successful or not, |delta_time| is the time takes complete the call
  // and bytes transferred
  //   On success, bytes transferred is number bytes returned to caller.
  //   On failure, bytes_transferred is the number of bytes requested by the
  //     caller.
  void UpdateReadStat(bool success, zx_ticks_t delta_time, uint64_t bytes_transferred) {
    if (Enabled() == false) {
      return;
    }
    read_.UpdateCallStat(success, delta_time, bytes_transferred);
  }

  void UpdateWriteStat(bool success, zx_ticks_t delta_time, uint64_t bytes_transferred) {
    if (Enabled() == false) {
      return;
    }
    write_.UpdateCallStat(success, delta_time, bytes_transferred);
  }

  void UpdateTrimStat(bool success, zx_ticks_t delta_time, uint64_t bytes_transferred) {
    if (Enabled() == false) {
      return;
    }
    trim_.UpdateCallStat(success, delta_time, bytes_transferred);
  }

  void UpdateFlushStat(bool success, zx_ticks_t delta_time, uint64_t bytes_transferred) {
    if (Enabled() == false) {
      return;
    }
    flush_.UpdateCallStat(success, delta_time, bytes_transferred);
  }

  void UpdateBarrierBeforeStat(bool success, zx_ticks_t delta_time, uint64_t bytes_transferred) {
    if (Enabled() == false) {
      return;
    }
    barrier_before_.UpdateCallStat(success, delta_time, bytes_transferred);
  }

  void UpdateBarrierAfterStat(bool success, zx_ticks_t delta_time, uint64_t bytes_transferred) {
    if (Enabled() == false) {
      return;
    }
    barrier_after_.UpdateCallStat(success, delta_time, bytes_transferred);
  }

  void UpdateStats(bool success, const zx::ticks start_tick, const uint32_t command,
                   const uint64_t bytes_transfered);

  // Total number of successful, failed, sum of successful and failed calls is returned if
  // |success| is true, false or nullopt, respectively.
  uint64_t TotalCalls(std::optional<bool> success = std::nullopt) const;

  // Total number of successful, failed, sum of successful and failed bytes transferred is
  // returned if |success| is true, false or nullopt respectively.
  uint64_t TotalBytesTransferred(std::optional<bool> success = std::nullopt) const;

  void Reset() {
    read_.Reset();
    write_.Reset();
    trim_.Reset();
    flush_.Reset();
    barrier_before_.Reset();
    barrier_after_.Reset();
  }

 private:
  CallStat read_ = {};            // stats for read
  CallStat write_ = {};           // stats for write
  CallStat trim_ = {};            // stats for trim
  CallStat flush_ = {};           // stats for flush
  CallStat barrier_before_ = {};  // stats for barrier before
  CallStat barrier_after_ = {};   // stats for barrier after
};

}  // namespace storage_metrics
#endif  // STORAGE_METRICS_BLOCK_METRICS_H_
