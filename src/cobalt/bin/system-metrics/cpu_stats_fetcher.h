// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_SYSTEM_METRICS_CPU_STATS_FETCHER_H_
#define SRC_COBALT_BIN_SYSTEM_METRICS_CPU_STATS_FETCHER_H_

#include <lib/zx/resource.h>

namespace cobalt {

// An abstrace interface for cpu stats fetching from various
// resources
class CpuStatsFetcher {
 public:
  virtual ~CpuStatsFetcher() = default;

  // Get average CPU percentage used over all CPU cores since
  // the last time this function is called.
  //
  // Return true if this is not the first time this function is
  // called. Pass the calculated percentage in cpu_percentage.
  // Return false if this is the first time this function is called
  // and there is not a time range during which we can calculate
  // the average CPU used.
  virtual bool FetchCpuPercentage(double *cpu_percentage) = 0;
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_SYSTEM_METRICS_CPU_STATS_FETCHER_H_
