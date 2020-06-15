// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "memory_stress.h"

#include <endian.h>
#include <fuchsia/kernel/cpp/fidl.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <random>
#include <string>
#include <thread>

#include <fbl/span.h>

#include "compiler.h"
#include "memory_range.h"
#include "memory_stats.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "status.h"
#include "temperature_sensor.h"
#include "util.h"

namespace hwstress {

namespace {

// Create a std::default_random_engine PRNG pre-seeded with a small truly random value.
std::default_random_engine CreateRandomEngine() {
  std::random_device rng;
  return std::default_random_engine(rng());
}

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
  result.exec = [pattern](StatusLine* /*unused*/, MemoryRange* memory) {
    // Write and verify the pattern followed by its negation.
    TestPatternAndComplement(memory, pattern);
  };
  result.memory_type = CacheMode::kCached;
  return result;
}

// Repeatedly open/close individual rows on a RAM bank to try and trigger bit errors.
//
// See https://en.wikipedia.org/wiki/Row_hammer for background.
void RowHammer(StatusLine* status, MemoryRange* memory, uint32_t iterations, uint64_t pattern) {
  constexpr int kAddressesPerIteration = 4;
  constexpr int kReadsPerIteration = 1'000'000;

  // Set all memory to the desired pattern.
  WritePattern(memory->span(), pattern);

  // Get random numbers returning a random page.
  uint32_t num_pages = memory->size_bytes() / ZX_PAGE_SIZE;
  std::default_random_engine rng = CreateRandomEngine();
  std::uniform_int_distribution<uint32_t> random_page(0, num_pages - 1);

  // Perform several iterations on different addresses before spending time
  // verifying memory.
  status->Verbose("Performing %d hammer iterations with pattern %016lx...", iterations, pattern);
  zx::time start = zx::clock::get_monotonic();
  for (uint32_t i = 0; i < iterations; i++) {
    // Select addresses to hammer.
    //
    // Our goal is to force the DRAM to open and close a single row many
    // times between a refresh cycle. We can do this by reading two
    // different rows on the same bank of RAM in quick succession.
    //
    // Because we don't know the layout of RAM, we select N random
    // pages, and read them quickly in succession. There is a good
    // chance we'll get lucky and end up with at least two rows in the
    // same bank.
    volatile uint32_t* targets[kAddressesPerIteration];
    uint8_t* data = memory->bytes();
    for (auto& target : targets) {
      target = reinterpret_cast<volatile uint32_t*>(data + random_page(rng) * ZX_PAGE_SIZE);
    }

    // Quickly activate the different rows.
    UNROLL_LOOP_4
    for (uint32_t j = 0; j < kReadsPerIteration; j++) {
      UNROLL_LOOP
      for (volatile uint32_t* target : targets) {
        ForceEval(*target);
      }
    };
  }
  zx::time end = zx::clock::get_monotonic();
  double seconds_per_iteration = DurationToSecs(end - start) / iterations;
  status->Verbose("Done. Time per iteration = %0.2fs, row activations per 64ms refresh ~= %0.0f",
                  seconds_per_iteration,
                  (kReadsPerIteration / seconds_per_iteration) * (64. / 1000.));

  // Ensure memory is still as expected.
  VerifyPattern(memory->span(), pattern);
}

MemoryWorkload MakeRowHammerWorkload(std::string_view name, uint64_t pattern) {
  MemoryWorkload result;
  result.name = std::string(name);
  result.exec = [pattern](StatusLine* status, MemoryRange* memory) {
    RowHammer(status, memory, /*iterations=*/100, pattern);
  };
  result.memory_type = CacheMode::kUncached;
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
    ZX_PANIC("Detected memory error: %s\n", result->c_str());
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

  // Row hammer workloads.
  result.push_back(MakeRowHammerWorkload("Row hammer, all bits clear", 0x0000'0000'0000'0000ul));
  result.push_back(MakeRowHammerWorkload("Row hammer, all bits set", 0xffff'ffff'ffff'fffful));
  result.push_back(
      MakeRowHammerWorkload("Row hammer, alternating bits (1/2)", 0xaaaa'aaaa'aaaa'aaaaul));
  result.push_back(
      MakeRowHammerWorkload("Row hammer, alternating bits (1/2)", 0x5555'5555'5555'5555ul));

  return result;
}

// Ensure that |result| contains at least |size| bytes of memory, mapped in as mode |mode|.
//
// Will deallocate and reallocate memory as required to achieve this.
zx::status<> ReallocateMemoryAsRequired(CacheMode mode, size_t size,
                                        std::unique_ptr<MemoryRange>* result) {
  // If we are already allocated and have the right cache mode, nothing to do.
  if (*result != nullptr && result->get()->cache() == mode && result->get()->size_bytes() >= size) {
    return zx::ok();
  }

  // Otherwise, allocate new memory.
  result->reset();
  zx::status<std::unique_ptr<MemoryRange>> maybe_memory = MemoryRange::Create(size, mode);
  if (maybe_memory.is_error()) {
    return zx::error(maybe_memory.status_value());
  }
  *result = std::move(maybe_memory).value();
  return zx::ok();
}

fitx::result<std::string, size_t> GetMemoryToTest(const CommandLineArgs& args) {
  // Get amount of RAM and free memory in system.
  zx::status<fuchsia::kernel::MemoryStats> maybe_stats = GetMemoryStats();
  if (maybe_stats.is_error()) {
    return fitx::error("Could not fetch free memory.");
  }

  // If a value was specified, and doesn't exceed total system RAM, use that.
  if (args.ram_to_test_megabytes.has_value()) {
    size_t requested = MiB(args.ram_to_test_megabytes.value());
    if (requested > maybe_stats->total_bytes()) {
      return fitx::error(fxl::StringPrintf(
          "Specified memory size (%ld bytes) exceeds system memory size (%ld bytes).", requested,
          maybe_stats->total_bytes()));
    }
    return fitx::ok(requested);
  }

  // If user asked for a percent of total memory, calculate that.
  if (args.ram_to_test_percent.has_value()) {
    uint64_t total_bytes = maybe_stats->total_bytes();
    auto test_bytes =
        static_cast<uint64_t>(total_bytes * (args.ram_to_test_percent.value() / 100.));
    return fitx::ok(RoundUp(test_bytes, ZX_PAGE_SIZE));
  }

  // Otherwise, try and calculate a reasonable value based on free memory.
  //
  // The default memory stress values for Fuchsia are:
  //   - 300MiB free => Warning
  //   - 150MiB free => Critical
  //   - 50MiB free => OOM
  //
  // We aim to hit just below the critical threshold.
  uint64_t free_bytes = maybe_stats->free_bytes();
  uint64_t slack = MiB(151);
  if (free_bytes < slack + MiB(1)) {
    // We don't have 150MiB free: just use 1MiB.
    return zx::ok(MiB(1));
  }
  return zx::ok(RoundUp(free_bytes - slack, ZX_PAGE_SIZE));
}

bool StressMemory(StatusLine* status, const CommandLineArgs& args, zx::duration duration,
                  TemperatureSensor* /*temperature_sensor*/) {
  // Parse the amount of memory to test.
  fitx::result<std::string, size_t> bytes_to_test = GetMemoryToTest(args);
  if (bytes_to_test.is_error()) {
    status->Log(bytes_to_test.error_value());
    return false;
  }
  status->Log("Testing %0.2fMiB of memory.", bytes_to_test.value() / static_cast<double>(MiB(1)));

  // Get workloads.
  size_t workload_index = 0;
  std::vector<MemoryWorkload> workloads = GenerateMemoryWorkloads();

  // Keep looping over the memory tests until we run out of time.
  std::unique_ptr<MemoryRange> memory;
  uint64_t num_tests = 1;
  zx::time end_time = zx::deadline_after(duration);
  while (zx::clock::get_monotonic() < end_time) {
    const MemoryWorkload& workload = workloads[workload_index];

    // Allocate memory for tests.
    zx::status<> result =
        ReallocateMemoryAsRequired(workload.memory_type, bytes_to_test.value(), &memory);
    if (result.is_error()) {
      status->Log("Failed to allocate memory: %s", result.status_string());
      return false;
    }

    status->Set("Test %4ld: %s", num_tests, workloads[workload_index].name.c_str());

    zx::time test_start = zx::clock::get_monotonic();
    workload.exec(status, memory.get());
    zx::time test_end = zx::clock::get_monotonic();

    status->Log("Test %4ld: %s: %0.3fs", num_tests, workloads[workload_index].name.c_str(),
                DurationToSecs(test_end - test_start));

    workload_index = (workload_index + 1) % workloads.size();
    num_tests++;
  }

  return true;
}

}  // namespace hwstress
