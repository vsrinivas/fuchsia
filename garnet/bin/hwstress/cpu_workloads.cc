// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpu_workloads.h"

#include <vector>

#include "cpu_stressor.h"

namespace hwstress {
namespace {

// A placeholder workload that just reads and writes memory.
void TrivialMemoryWorkload(const StopIndicator& indicator) {
  volatile uint64_t a = 0;
  do {
    a++;
  } while (!indicator.ShouldStop());
}

// A placeholder workload that just spins.
void TrivialSpinWorkload(const StopIndicator& indicator) {
  do {
    /* do nothing */
  } while (!indicator.ShouldStop());
}

}  // namespace

std::vector<Workload> GetWorkloads() {
  return std::vector<Workload>{{"spin", TrivialSpinWorkload}, {"memory", TrivialMemoryWorkload}};
}

}  // namespace hwstress
