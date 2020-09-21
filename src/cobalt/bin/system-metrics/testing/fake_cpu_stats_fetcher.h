// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_SYSTEM_METRICS_TESTING_FAKE_CPU_STATS_FETCHER_H_
#define SRC_COBALT_BIN_SYSTEM_METRICS_TESTING_FAKE_CPU_STATS_FETCHER_H_

#include "src/cobalt/bin/system-metrics/cpu_stats_fetcher.h"

namespace cobalt {

class FakeCpuStatsFetcher : public cobalt::CpuStatsFetcher {
 public:
  FakeCpuStatsFetcher();
  FetchCpuResult FetchCpuPercentage(double *cpu_percentage) override;
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_SYSTEM_METRICS_TESTING_FAKE_CPU_STATS_FETCHER_H_
