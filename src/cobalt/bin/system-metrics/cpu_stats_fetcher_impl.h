// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_SYSTEM_METRICS_CPU_STATS_FETCHER_IMPL_H_
#define SRC_COBALT_BIN_SYSTEM_METRICS_CPU_STATS_FETCHER_IMPL_H_

#include <lib/zx/resource.h>

#include <chrono>
#include <vector>

#include "src/cobalt/bin/system-metrics/cpu_stats_fetcher.h"

using cobalt::CpuStatsFetcher;

namespace cobalt {

class CpuStatsFetcherImpl : public CpuStatsFetcher {
 public:
  CpuStatsFetcherImpl();
  bool FetchCpuPercentage(double *cpu_percentage) override;

 private:
  bool FetchCpuCoreCount();
  bool FetchCpuStats();
  bool CalculateCpuPercentage(double *cpu_percentage);
  void InitializeRootResourceHandle();

  zx_handle_t root_resource_handle_ = ZX_HANDLE_INVALID;
  size_t num_cpu_cores_ = 0;
  std::chrono::time_point<std::chrono::high_resolution_clock> cpu_fetch_time_;
  std::chrono::time_point<std::chrono::high_resolution_clock>
      last_cpu_fetch_time_;
  // TODO: Determine the vector size at runtime (32 is arbitrary).
  std::vector<zx_info_cpu_stats_t> cpu_stats_{32};
  std::vector<zx_info_cpu_stats_t> last_cpu_stats_{32};
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_SYSTEM_METRICS_CPU_STATS_FETCHER_IMPL_H_
