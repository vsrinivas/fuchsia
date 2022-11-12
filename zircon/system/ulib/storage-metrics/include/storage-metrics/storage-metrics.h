// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef STORAGE_METRICS_STORAGE_METRICS_H_
#define STORAGE_METRICS_STORAGE_METRICS_H_

#include <fidl/fuchsia.storage.metrics/cpp/wire.h>
#include <stdio.h>

#include <atomic>
#include <limits>
#include <optional>

#include <fbl/macros.h>

namespace storage_metrics {
constexpr zx_ticks_t kUninitializedMinimumLatency = std::numeric_limits<zx_ticks_t>::max();
using CallStatFidl = fuchsia_storage_metrics::wire::CallStat;
using CallStatRawFidl = fuchsia_storage_metrics::wire::CallStatRaw;

// Compares total_calls and bytes_transferred. Returns false if they don't match.
bool RawCallStatEqual(const CallStatRawFidl& lhs, const CallStatRawFidl& rhs);

// Compare raw stats for success and failure. Returns false if they don't match.
bool CallStatEqual(const CallStatFidl& lhs, const CallStatFidl& rhs);

// The class provides light weight mechanism to maintain stats for system calls.
// The updates are pseudo-atomic - meaning each field gets updated atomically
// but class as a whole may not get updated atomically, This will allow class
// to be updated in a lock-free manner in almost all intended use cases
// i.e. filesystem and block device.
class CallStat {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(CallStat);

  CallStat();
  ~CallStat() = default;

  // Copies fields of fidl structure into corresponding fields of CallStat
  void CopyFromFidl(const CallStatFidl* istat);

  // Copies to fields of fidl structure the corresponding fields of CallStat
  void CopyToFidl(CallStatFidl* out) const;

  // Prints the fields of CallStat to file |stream|. If |success| is nullopt,
  // prints aggregate of successful and failed calls. If |success| is true,
  // then only stats from success_stat_ are printed otherwise failure_stat_
  // are printed.
  void Dump(FILE* stream, const char* stat_name, std::optional<bool> success = std::nullopt) const;

  // Prints stats of both success_stat_ and failure_stat_ to |stream| file.
  void DumpAll(FILE* stream, const char* stat_name) const;

  // Updates fields of success_stat_ or of failure_stat_ if |success| is true
  // or false respectively.
  void UpdateCallStat(bool success, zx_ticks_t delta_time, uint64_t bytes_transferred);

  // Resets the stats to initial state.
  void Reset();

  // Following functions returns fields within CallStatRaw. If |success| is
  // true then success_stat_ is chosen for CallStatRaw, is false then
  // failures_stat_ is chosen for CallStatRaw, or if ||success| is nullopt
  // then an aggregate of success_stat_ and failures_stat_ is returned.
  zx_ticks_t minimum_latency(std::optional<bool> success = std::nullopt) const;
  zx_ticks_t maximum_latency(std::optional<bool> success = std::nullopt) const;
  zx_ticks_t total_time_spent(std::optional<bool> success = std::nullopt) const;
  uint64_t total_calls(std::optional<bool> success = std::nullopt) const;
  uint64_t bytes_transferred(std::optional<bool> success = std::nullopt) const;

 private:
  // This is atomic C++ class version of fuchsia_storage_metrics_CallStat
  struct CallStatRaw {
    DISALLOW_COPY_ASSIGN_AND_MOVE(CallStatRaw);

    CallStatRaw();

    void Reset();

    // Copies fields of fidl structure into corresponding fields of CallStat
    void CopyFromRawFidl(const CallStatRawFidl* istat);

    // Copies to fields of fidl structure the corresponding fields of CallStat
    void CopyToRawFidl(CallStatRawFidl* out) const;

    void UpdateRawCallStat(zx_ticks_t delta_time, uint64_t bytes_transferred);

    // Minimum time taken by a request to be served.
    std::atomic<zx_ticks_t> minimum_latency;

    // Maximum time taken by a request to be served.
    std::atomic<zx_ticks_t> maximum_latency;

    // Total time spent to serve requests.
    std::atomic<zx_ticks_t> total_time_spent;

    // Total number of calls.
    std::atomic<uint64_t> total_calls;

    // bytes_transferred has special meaning if the succeeded or failed.
    // On success:
    //    Partitally succeeded calls, bytes fetched is less than bytes
    //    requested, can be considered as successful. To keep latency and
    //    time_spent numbers accurate, on success, bytes transferred is
    //    number bytes returned to caller. It is NOT the number of bytes
    //    fetched from underlying subsystem and it is NOT number of bytes
    //     requested by the caller.
    // On failure:
    //    On failure, bytes_transferred is the number of bytes requested by
    //    the caller.
    std::atomic<uint64_t> bytes_transferred;
  };

  // success_stat_ keeps track of successful calls.
  CallStatRaw success_stat_;

  // failure_stat_ keeps tracks of failed calls.
  CallStatRaw failure_stat_;
};

class Metrics {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(Metrics);

  Metrics() = default;
  ~Metrics() = default;
  void SetEnable(bool enable);
  bool Enabled() const;

 private:
  std::atomic<bool> enabled_ = true;
};

}  // namespace storage_metrics
#endif  // STORAGE_METRICS_STORAGE_METRICS_H_
