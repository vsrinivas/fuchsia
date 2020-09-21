// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_SYSTEM_METRICS_CPU_STATS_FETCHER_H_
#define SRC_COBALT_BIN_SYSTEM_METRICS_CPU_STATS_FETCHER_H_

#include <lib/zx/resource.h>

namespace cobalt {

enum class FetchCpuResult {
  Ok,
  FirstDataPoint,
  Error,
};

// An abstrace interface for cpu stats fetching from various
// resources
class CpuStatsFetcher {
 public:
  virtual ~CpuStatsFetcher() = default;

  // Get average CPU percentage used over all CPU cores since
  // the last time this function is called.
  //
  // If it is not the first time this function is called, the return value will be Ok and
  // cpu_percentage will be populated with the calculated CPU usage since the last call. If it is
  // the first time this function is called, the return value will be FirstDataPoint and
  // cpu_percentage will be unchanged. If an error occurs, the return value will be Error.
  virtual FetchCpuResult FetchCpuPercentage(double *cpu_percentage) = 0;
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_SYSTEM_METRICS_CPU_STATS_FETCHER_H_
