// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_HWSTRESS_MEMORY_STRESS_H_
#define GARNET_BIN_HWSTRESS_MEMORY_STRESS_H_

#include <lib/zx/time.h>

#include <optional>
#include <vector>

#include "args.h"
#include "memory_range.h"
#include "status.h"
#include "temperature_sensor.h"

namespace hwstress {

// Start a memory stress test.
//
// Return true on success.
bool StressMemory(StatusLine* status, const CommandLineArgs& args, zx::duration duration_seconds,
                  TemperatureSensor* sensor = GetNullTemperatureSensor());

//
// Exposed for testing.
//

// A memory stress workload.
struct MemoryWorkload {
  // Human-readable name of the workload.
  std::string name;

  // Execute the workload.
  std::function<void(StatusLine* line, zx::duration, MemoryRange* memory)> exec;

  // Memory type needed for the test.
  CacheMode memory_type;

  // Should we report the throughput of the test?
  //
  // Some tests don't make sense for throughput reporting, so can just set this to
  // false.
  bool report_throughput = true;
};

// Get all memory stress workloads.
std::vector<MemoryWorkload> GenerateMemoryWorkloads();

// Generates combinations of (workloads, cpu_number) to ensure an even coverage
// of both.
//
// For example, given the workloads [A, B] and 2 cpus, subsequent calls to |Next| will
// return the values:
//
//   [{A, 0}, {B, 1}, {A, 1}, {B, 0}]
//
// and then repeat the sequence.
class MemoryWorkloadGenerator {
 public:
  // Generate combinations from the given list of workloads / number of CPUs.
  explicit MemoryWorkloadGenerator(const std::vector<MemoryWorkload>& workloads, uint32_t num_cpus);

  // Generate the next combination.
  struct Workload {
    uint32_t cpu;
    MemoryWorkload& workload;
  };
  Workload Next();

 private:
  std::vector<std::optional<MemoryWorkload>> workloads_;
  uint32_t num_cpus_;
  uint64_t n_ = -1;  // start at -1 so first increment puts us at 0.
};

}  // namespace hwstress

#endif  // GARNET_BIN_HWSTRESS_MEMORY_STRESS_H_
