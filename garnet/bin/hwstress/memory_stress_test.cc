// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "memory_stress.h"

#include <lib/zx/time.h>

#include <initializer_list>
#include <vector>

#include <gtest/gtest.h>

#include "memory_range.h"
#include "status.h"

namespace hwstress {
namespace {

TEST(Memory, GenerateMemoryWorkloads) {
  StatusLine status;

  // Generate workloads, and exercise each on 4096 bytes of RAM.
  std::unique_ptr<MemoryRange> memory =
      MemoryRange::Create(zx_system_get_page_size(), CacheMode::kCached).value();
  for (const MemoryWorkload& workload : GenerateMemoryWorkloads()) {
    workload.exec(&status, /*max_duration=*/zx::msec(10), memory.get());
  }
}

TEST(Memory, WorkloadGenerator) {
  // Create a generator with 3 workloads and 3 CPUs.
  MemoryWorkloadGenerator generator(
      std::vector<MemoryWorkload>{
          MemoryWorkload{.name = "A"},
          MemoryWorkload{.name = "B"},
          MemoryWorkload{.name = "C"},
      },
      3);

  // Ensure we get coverage across all workloads and CPUs.
  for (const std::pair<std::string, uint32_t>& expected :
       std::initializer_list<std::pair<std::string, uint32_t>>{
           {"A", 0},
           {"B", 1},
           {"C", 2},
           {"A", 1},
           {"B", 2},
           {"C", 0},
           {"A", 2},
           {"B", 0},
           {"C", 1},
       }) {
    MemoryWorkloadGenerator::Workload w = generator.Next();
    EXPECT_EQ(expected, std::make_pair(w.workload.name, w.cpu));
  }
}

TEST(Memory, StressMemory) {
  // Exercise the main StressMemory function for a tiny amount of time and memory.
  CommandLineArgs args;
  args.mem_to_test_megabytes = 1;

  StatusLine status;
  EXPECT_TRUE(StressMemory(&status, args, zx::msec(1)));
}

}  // namespace
}  // namespace hwstress
