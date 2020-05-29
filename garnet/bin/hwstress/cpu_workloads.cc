// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpu_workloads.h"

#include <vector>

namespace hwstress {
namespace {

// A placeholder workload that just reads and writes memory.
void TrivialMemoryWorkload() {
  volatile uint64_t a = 0;
  for (int i = 0; i < 10'000; i++) {
    a++;
  }
}

// A placeholder workload that just spins.
void TrivialSpinWorkload() {
  for (int i = 0; i < 10'000; i++) {
    __asm__ __volatile__("");
  }
}

}  // namespace

std::vector<Workload> GetWorkloads() {
  return std::vector<Workload>{{"spin", TrivialSpinWorkload}, {"memory", TrivialMemoryWorkload}};
}

}  // namespace hwstress
