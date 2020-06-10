// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "memory_stress.h"

#include <endian.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <random>
#include <thread>

#include <fbl/span.h>

#include "compiler.h"
#include "memory_range.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "status.h"
#include "temperature_sensor.h"
#include "util.h"

namespace hwstress {

namespace {

// Writes a pattern to memory; verifies it is still the same, and writes out the
// complement; and finally verify the complement has been correctly written out.
void TestPatternAndComplement(MemoryRange* memory, uint64_t pattern) {
  // Convert pattern to big-endian.
  uint64_t be_pattern = htobe64(pattern);

  // Write out the pattern.
  WritePattern(memory->span(), pattern);
  memory->CleanInvalidateCache();

  // Verify the pattern, flipping each word as we progress.
  //
  // We perform a read/verify/write on each word at a time (instead of
  // a VerifyPattern/WritePattern pair) to minimise the time between
  // verifying the old value and writing the next test pattern.
  uint64_t* start = memory->words();
  size_t words = memory->size_words();
  for (size_t i = 0; i < words; i++) {
    if (unlikely(start[i] != be_pattern)) {
      ZX_PANIC("Found memory error: expected 0x%16lx, got 0x%16lx at offset %ld.\n", pattern,
               start[i], i);
    }
    start[i] = ~be_pattern;
  }
  memory->CleanInvalidateCache();

  // Verify the pattern again.
  VerifyPattern(memory->span(), ~pattern);
}

// Make a |MemoryWorkload| consisting of writing a simple pattern out to RAM
// and reading it again.
MemoryWorkload MakeSimplePatternWorkload(std::string_view name, uint64_t pattern) {
  MemoryWorkload result;
  result.name = std::string(name);
  result.exec = [pattern](MemoryRange* memory) {
    // Write and verify the pattern followed by its negation.
    TestPatternAndComplement(memory, pattern);
  };
  return result;
}

}  // namespace

void WritePattern(fbl::Span<uint8_t> range, uint64_t pattern) {
  // Hide the pattern from the compiler.
  //
  // This prevents the compiler from optimising this into a |memset|. When
  // the pattern is |0|, |memset| will try and optimise itself by zero'ing
  // cache lines instead of writing memory. This fails when we attempt to
  // write to uncached memory.
  pattern = HideFromCompiler(pattern);

  // Convert pattern to big-endian.
  pattern = htobe64(pattern);

  // Write out the pattern.
  auto* start = reinterpret_cast<uint64_t*>(range.begin());
  size_t words = range.size_bytes() / sizeof(uint64_t);
  for (size_t i = 0; i < words; i++) {
    start[i] = pattern;
  }
}

std::optional<std::string> VerifyPattern(fbl::Span<uint8_t> range, uint64_t pattern) {
  auto* start = reinterpret_cast<uint64_t*>(range.begin());
  size_t words = range.size_bytes() / sizeof(uint64_t);

  // Convert pattern to big-endian.
  pattern = htobe64(pattern);

  // We use a branchless fast path, which is optimised to assume that everything matches.
  //
  // If we actually find a mismatch, we scan again with a slow path to find the
  // actual error.
  uint64_t mismatches = 0;
  for (size_t i = 0; i < words; i++) {
    mismatches += (start[i] != pattern);
  }

  // If we found a mismatch, search again to find it.
  if (unlikely(mismatches != 0)) {
    for (size_t i = 0; i < words; i++) {
      if (start[i] != pattern) {
        return fxl::StringPrintf("Expected 0x%016lx, got 0x%16lx at offset %ld.", pattern, start[i],
                                 i * sizeof(uint64_t));
      }
    }
    return fxl::StringPrintf("Detected %ld transient bitflip(s) at unknown location(s).",
                             mismatches);
  }

  return std::nullopt;
}

void VerifyPatternOrDie(fbl::Span<uint8_t> range, uint64_t pattern) {
  std::optional<std::string> result = VerifyPattern(range, pattern);
  if (unlikely(result.has_value())) {
    ZX_PANIC("Detected memory error: %s\n", result.value().c_str());
  }
}

std::vector<MemoryWorkload> GenerateMemoryWorkloads() {
  std::vector<MemoryWorkload> result;

  // Generate some simple patterns.
  struct BitPattern {
    std::string_view name;
    uint64_t pattern;
  };
  for (const auto& pattern : std::initializer_list<BitPattern>{
           {"All 0 bits", 0x0000'0000'0000'0000},
           {"All 1 bits", 0xffff'ffff'ffff'fffful},
           {"Alternating bits (1/2)", 0x5555'5555'5555'5555},
           {"Alternating bits (2/2)", 0xaaaa'aaaa'aaaa'aaaa},
           {"2 bits on / 2 bits off (1/2)", 0x3333'3333'3333'3333ul},
           {"2 bits on / 2 bits off (2/2)", 0xcccc'cccc'cccc'ccccul},
           {"4 bits on / 4 bits off (1/2)", 0xf0f0'f0f0'f0f0'f0f0ul},
           {"4 bits on / 4 bits off (2/2)", 0x0f0f'0f0f'0f0f'0f0ful},
       }) {
    result.push_back(MakeSimplePatternWorkload(pattern.name, pattern.pattern));
  }

  // Single bits set/clear.
  for (uint64_t i = 0; i < 8; i++) {
    result.push_back(MakeSimplePatternWorkload(
        fxl::StringPrintf("Single bit set 8-bit (%ld/8)", i + 1), RepeatByte(1ul << i)));
  }
  for (uint64_t i = 0; i < 8; i++) {
    result.push_back(MakeSimplePatternWorkload(
        fxl::StringPrintf("Single bit set 8-bit (%ld/8)", i + 1), RepeatByte(1ul << i)));
  }

  return result;
}

bool StressMemory(zx::duration duration, TemperatureSensor* /*temperature_sensor*/) {
  StatusLine status;

  // Get workloads.
  size_t workload_index = 0;
  std::vector<MemoryWorkload> workloads = GenerateMemoryWorkloads();

  // Allocate memory for tests.
  constexpr size_t kMemoryToTest = 32 * 1024 * 1024;  // 32 MiB.
  zx::status<std::unique_ptr<MemoryRange>> maybe_memory =
      MemoryRange::Create(kMemoryToTest, CacheMode::kCached);
  if (maybe_memory.is_error()) {
    status.Log("Failed to allocate memory: %s", maybe_memory.status_string());
    return false;
  }
  MemoryRange* memory = maybe_memory.value().get();

  // Keep looping until we run out of time.
  uint64_t num_tests = 1;
  zx::time end_time = zx::deadline_after(duration);
  while (zx::clock::get_monotonic() < end_time) {
    status.Set("Test %4ld: %s", num_tests, workloads[workload_index].name.c_str());

    zx::time test_start = zx::clock::get_monotonic();
    workloads[workload_index].exec(memory);
    zx::time test_end = zx::clock::get_monotonic();

    status.Log("Test %4ld: %s: %0.3fs", num_tests, workloads[workload_index].name.c_str(),
               DurationToSecs(test_end - test_start));

    workload_index = (workload_index + 1) % workloads.size();
    num_tests++;
  }

  return true;
}

}  // namespace hwstress
