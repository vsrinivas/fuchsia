// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpu_workloads.h"

#include <stdio.h>

#include <string>

#include <gtest/gtest.h>

namespace hwstress {
namespace {

TEST(CpuWorkloads, Test) {
  // Test one iteration of each of the workloads.
  for (const auto& workload : GetWorkloads()) {
    printf("  Testing %s... ", workload.name.c_str());
    fflush(stdout);
    workload.work();
    printf("done.\n");
  }
}

}  // namespace
}  // namespace hwstress
