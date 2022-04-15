// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpu_workloads.h"

#include <stdio.h>

#include <string>

#include <gtest/gtest.h>

#include "cpu_stressor.h"

namespace hwstress {
namespace {

TEST(CpuWorkloads, Test) {
  StopIndicator stop;
  WorkIndicator indicator(stop, 1.0);
  stop.Stop();

  // Test one iteration of each of the workloads.
  //
  // Even though the stop indicator is already set, each workload should
  // unconditionally perform one iteration before returning.
  for (const auto& workload : GetCpuWorkloads()) {
    printf("  Testing %s... ", workload.name.c_str());
    fflush(stdout);
    workload.work(indicator);
    printf("done.\n");
  }
}

}  // namespace
}  // namespace hwstress
