// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_HWSTRESS_MEMORY_STRESS_H_
#define GARNET_BIN_HWSTRESS_MEMORY_STRESS_H_

#include <lib/zx/time.h>

#include <optional>
#include <vector>

#include "memory_range.h"
#include "status.h"
#include "temperature_sensor.h"

namespace hwstress {

// Start a memory stress test.
//
// Return true on success.
bool StressMemory(StatusLine* status, zx::duration duration_seconds,
                  TemperatureSensor* sensor = GetNullTemperatureSensor());

//
// Exposed for testing.
//

// A memory stress workload.
struct MemoryWorkload {
  // Human-readable name of the workload.
  std::string name;

  // Execute the workload.
  std::function<void(MemoryRange* memory)> exec;
};

// Get all memory stress workloads.
std::vector<MemoryWorkload> GenerateMemoryWorkloads();

// Write the given pattern out to memory.
//
// Pattern is always written in memory as a big-endian word. That is, the
// pattern 0x11223344556677 will be written out as bytes 0x11, 0x22, ..., 0x77
// at increasing memory addresses.
//
// Range must be page aligned and a multiple of a page size.
void WritePattern(fbl::Span<uint8_t> range, uint64_t pattern);

// Verify memory has the given pattern written to it.
//
// Like |WritePattern|, we assume that pattern has been written out in memory
// in a big endian format.
//
// Range must be page aligned and a multiple of a page size.
//
// On success, returns "std::nullopt". On failure, returns a string describing the error.
//
// The |VerifyPatternOrDie| variant aborts on error instead of returning.
std::optional<std::string> VerifyPattern(fbl::Span<uint8_t> range, uint64_t pattern);
void VerifyPatternOrDie(fbl::Span<uint8_t> range, uint64_t pattern);

}  // namespace hwstress

#endif  // GARNET_BIN_HWSTRESS_MEMORY_STRESS_H_
