// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_STORAGE_METRICS_FS_METRICS_H_
#define ZIRCON_SYSTEM_ULIB_STORAGE_METRICS_FS_METRICS_H_

#include <atomic>
#include <stdio.h>

#include <fbl/macros.h>
#include <fuchsia/minfs/c/fidl.h>
#include <fuchsia/minfs/llcpp/fidl.h>
#include <storage-metrics/storage-metrics.h>

namespace storage_metrics {
using storage_metrics::Metrics;

// FsMetrics is common metrics that can be used across filesystems. The members
// are internded to stay generic.
class FsMetrics : public Metrics {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(FsMetrics);

  FsMetrics() = default;
  explicit FsMetrics(const ::llcpp::fuchsia::storage::metrics::FsMetrics* metrics);
  explicit FsMetrics(const fuchsia_storage_metrics_FsMetrics* metrics);
  ~FsMetrics() = default;

  // Copies to fields of fidl structure the corresponding fields of FsMetrics.
  void CopyToFidl(::llcpp::fuchsia::storage::metrics::FsMetrics* metrics) const;

  // Copies to fields of fidl structure the corresponding fields of FsMetrics.
  void CopyToFidl(fuchsia_storage_metrics_FsMetrics* metrics) const;

  // Prints all CallStat fields to file |stream|. Passes |success| to
  // CallStat::Dump. See CallStat::Dump.
  void Dump(FILE* stream, std::optional<bool> success = std::nullopt) const;

  // Following Updata*Stat functions take |success|, which denotes whether the
  // call was successful or not, |delta_time| is the time takes complete the call
  // and bytes transferred
  //   On success, bytes transferred is number bytes returned to caller. It is
  //     NOT the number of bytes fetched from underlying subsystem and it
  //     is NOT number of bytes requested by the caller.
  //   On failure, bytes_transferred is the number of bytes requested by the
  //     caller.
  void UpdateCreateStat(bool success, zx_ticks_t delta_time, uint64_t bytes_transferred) {
    if (Enabled() == false) {
      return;
    }
    create_.UpdateCallStat(success, delta_time, bytes_transferred);
  }

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

  void UpdateTruncateStat(bool success, zx_ticks_t delta_time, uint64_t bytes_transferred) {
    if (Enabled() == false) {
      return;
    }
    truncate_.UpdateCallStat(success, delta_time, bytes_transferred);
  }

  void UpdateUnlinkStat(bool success, zx_ticks_t delta_time, uint64_t bytes_transferred) {
    if (Enabled() == false) {
      return;
    }
    unlink_.UpdateCallStat(success, delta_time, bytes_transferred);
  }

  void UpdateRenameStat(bool success, zx_ticks_t delta_time, uint64_t bytes_transferred) {
    if (Enabled() == false) {
      return;
    }
    rename_.UpdateCallStat(success, delta_time, bytes_transferred);
  }

  void UpdateLookupStat(bool success, zx_ticks_t delta_time, uint64_t bytes_transferred) {
    if (Enabled() == false) {
      return;
    }
    lookup_.UpdateCallStat(success, delta_time, bytes_transferred);
  }

  void UpdateOpenStat(bool success, zx_ticks_t delta_time, uint64_t bytes_transferred) {
    if (Enabled() == false) {
      return;
    }
    open_.UpdateCallStat(success, delta_time, bytes_transferred);
  }

 private:
  CallStat create_ = {};    // stats for create
  CallStat read_ = {};      // stats for read
  CallStat write_ = {};     // stats for write
  CallStat truncate_ = {};  // stats for truncate
  CallStat unlink_ = {};    // stats for unlink
  CallStat rename_ = {};    // stats for rename
  CallStat lookup_ = {};    // stats for lookup
  CallStat open_ = {};      // stats for open
};

}  // namespace storage_metrics
#endif  // ZIRCON_SYSTEM_ULIB_STORAGE_METRICS_FS_METRICS_H_
