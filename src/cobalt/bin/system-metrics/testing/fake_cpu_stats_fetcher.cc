// Copyright 2019  The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/system-metrics/testing/fake_cpu_stats_fetcher.h"

namespace cobalt {

FakeCpuStatsFetcher::FakeCpuStatsFetcher() {}

FetchCpuResult FakeCpuStatsFetcher::FetchCpuPercentage(double *cpu_percentage) {
  *cpu_percentage = 12.34;
  return FetchCpuResult::Ok;
}

}  // namespace cobalt
