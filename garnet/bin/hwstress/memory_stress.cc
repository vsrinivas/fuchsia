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

#include <numeric>
#include <random>
#include <string>
#include <thread>

#include <fbl/span.h>

#include "compiler.h"
#include "memory_patterns.h"
#include "memory_range.h"
#include "memory_stats.h"
#include "profile_manager.h"
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
template <typename PatternGenerator>
void TestPatternAndComplement(MemoryRange* memory, PatternGenerator pattern) {
  PatternGenerator original_pattern = pattern;

  // Write out the pattern.
  WritePattern(memory->span(), pattern);
  memory->CleanInvalidateCache();

  // Verify the pattern, flipping each word as we progress.
  //
  // We perform a read/verify/write on each word at a time (instead of
  // a VerifyPattern/WritePattern pair) to minimise the time between
  // verifying the old value and writing the next test pattern.
  {
    uint64_t* start = memory->words();
    size_t words = memory->size_words();
    for (size_t i = 0; i < words; i++) {
      uint64_t p = pattern();
      if (unlikely(start[i] != p)) {
        ZX_PANIC("Found memory error: expected 0x%16lx, got 0x%16lx at offset %ld.\n", p, start[i],
                 i);
      }
      start[i] = ~p;
    }
    memory->CleanInvalidateCache();
  }

  // Verify the pattern again.
  VerifyPattern(memory->span(), InvertPattern(original_pattern));
}

// Make a |MemoryWorkload| consisting of writing a pattern to RAM
// and reading it again.
template <typename PatternGenerator>
MemoryWorkload MakePatternWorkload(std::string_view name, PatternGenerator pattern) {
  MemoryWorkload result;
  result.name = std::string(name);
  result.exec = [pattern](StatusLine* /*unused*/, zx::duration /*unused*/, MemoryRange* memory) {
    // Write and verify the pattern followed by its negation.
    TestPatternAndComplement(memory, pattern);
  };
  result.memory_type = CacheMode::kCached;
  return result;
}

// Repeatedly open/close individual rows on a RAM bank to try and trigger bit errors.
//
// See https://en.wikipedia.org/wiki/Row_hammer for background.
void RowHammer(StatusLine* status, MemoryRange* memory, zx::duration duration, uint64_t pattern) {
  constexpr int kAddressesPerIteration = 4;
  constexpr int kReadsPerIteration = 1'000'000;

  // Set all memory to the desired pattern.
  WritePattern(memory->span(), SimplePattern(pattern));

  // Get random numbers returning a random page.
  uint32_t num_pages = memory->size_bytes() / ZX_PAGE_SIZE;
  std::default_random_engine rng = CreateRandomEngine();
  std::uniform_int_distribution<uint32_t> random_page(0, num_pages - 1);

  // Perform several iterations on different addresses before spending time
  // verifying memory.
  status->Verbose("Performing RowHammer for %0.2fs with pattern %016lx...",
                  DurationToSecs(duration), pattern);
  zx::time start = zx::clock::get_monotonic();
  uint64_t iterations = 0;
  while (zx::clock::get_monotonic() - start < duration) {
    iterations++;

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
  VerifyPattern(memory->span(), SimplePattern(pattern));
}

MemoryWorkload MakeRowHammerWorkload(std::string_view name, uint64_t pattern) {
  return MemoryWorkload{
      .name = std::string(name),

      // Execute the main RowHammer function.
      .exec =
          [pattern](StatusLine* status, zx::duration max_duration, MemoryRange* memory) {
            RowHammer(status, memory, /*duration=*/std::min(max_duration, zx::sec(30)), pattern);
          },

      // Need to use uncached memory to ensure that each access hits main memory.
      .memory_type = CacheMode::kUncached,

      // We do not run in time proportional to the memory size, so don't
      // attempt to report throughput numbers.
      .report_throughput = false,
  };
}

}  // namespace

template <typename PatternGenerator>
void VerifyPatternOrDie(fbl::Span<uint8_t> range, PatternGenerator pattern) {
  std::optional<std::string> result = VerifyPattern(range, pattern);
  if (unlikely(result.has_value())) {
    ZX_PANIC("Detected memory error: %s\n", result->c_str());
  }
}

std::vector<MemoryWorkload> GenerateMemoryWorkloads() {
  std::vector<MemoryWorkload> result;

  // Simple patterns.
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
    result.push_back(MakePatternWorkload(pattern.name, SimplePattern(pattern.pattern)));
  }

  // 1 in 6 bits set.
  //
  // Having every 6'th bit set results in rows of RAM not having bits to
  // the north/south/east/west set, assuming that the rows are
  // a power-of-two size.
  auto every_sixth_bit = std::initializer_list<uint64_t>{
      0b1000001000001000001000001000001000001000001000001000001000001000,
      0b0010000010000010000010000010000010000010000010000010000010000010,
      0b0000100000100000100000100000100000100000100000100000100000100000,
  };
  for (int i = 0; i < 6; i++) {
    result.push_back(MakePatternWorkload(fxl::StringPrintf("Single bit set 6-bit (%d/6)", i + 1),
                                         MultiWordPattern(RotatePattern(every_sixth_bit, i))));
  }
  for (int i = 0; i < 6; i++) {
    result.push_back(
        MakePatternWorkload(fxl::StringPrintf("Single bit clear 6-bit (%d/6)", i + 1),
                            MultiWordPattern(NegateWords((RotatePattern(every_sixth_bit, i))))));
  }

  // Random bits.
  constexpr int kRandomBitIterations = 10;
  for (int i = 0; i < kRandomBitIterations; i++) {
    result.push_back(MakePatternWorkload(
        fxl::StringPrintf("Random bits (%d/%d)", i + 1, kRandomBitIterations), RandomPattern()));
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
  if (args.mem_to_test_megabytes.has_value()) {
    size_t requested = MiB(args.mem_to_test_megabytes.value());
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
  // We aim to hit just below the warning threshold.
  uint64_t free_bytes = maybe_stats->free_bytes();
  uint64_t slack = MiB(301);
  if (free_bytes < slack + MiB(1)) {
    // We don't have 300MiB free: just use 1MiB.
    return zx::ok(MiB(1));
  }
  return zx::ok(RoundUp(free_bytes - slack, ZX_PAGE_SIZE));
}

MemoryWorkloadGenerator ::MemoryWorkloadGenerator(const std::vector<MemoryWorkload>& workloads,
                                                  uint32_t num_cpus)
    : num_cpus_(num_cpus) {
  ZX_ASSERT(!workloads.empty());

  // Copy workloads into workloads_, converting into
  // a std::optional<MemoryWorkload> in the process.
  workloads_.reserve(workloads.size());
  for (const MemoryWorkload& workload : workloads) {
    workloads_.emplace_back(workload);
  }

  // We want to iterate through different workloads and different CPUs. One
  // method would be to test 1 CPU through all workloads, or 1 workload
  // through all CPUs. Neither is great: ideally, we would like to get
  // good coverage of both CPUs and workloads relatively quickly.
  //
  // To try and quickly maximise coverage, we instead iterate through both
  // simultaneously. If we have:
  //
  //   gcd(num_cpus, num_workloads) == 1
  //
  // then the Chinese Remainder Theorem [1] ensures that after num_cpus
  // * num_workloads iterations, we will have covered every combination of
  // num_cpus * num_workloads.
  //
  // To ensure that gcd(num_cpus, num_workloads) == 1, we keep adding a number
  // of dummy "null" workloads until this criteria is met.
  //
  // [1] https://en.wikipedia.org/wiki/Chinese_remainder_theorem
  while (std::gcd(num_cpus_, workloads_.size()) != 1) {
    workloads_.push_back(std::nullopt);
  }
}

MemoryWorkloadGenerator::Workload MemoryWorkloadGenerator::Next() {
  // Increment |n|, skipping over null workloads.
  do {
    n_++;
  } while (!workloads_[n_ % workloads_.size()].has_value());

  return Workload{
      .cpu = static_cast<uint32_t>(n_ % num_cpus_),
      .workload = workloads_[n_ % workloads_.size()].value(),
  };
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

  // Create a profile manager.
  std::unique_ptr<ProfileManager> profile_manager = ProfileManager::CreateFromEnvironment();
  if (profile_manager == nullptr) {
    status->Log("Error: could not create profile manager.");
    return false;
  }

  // Get workloads.
  std::vector<MemoryWorkload> workloads = GenerateMemoryWorkloads();
  MemoryWorkloadGenerator workload_generator{workloads, zx_system_get_num_cpus()};

  // Keep looping over the memory tests until we run out of time.
  std::unique_ptr<MemoryRange> memory;
  uint64_t num_tests = 1;
  zx::time end_time = zx::deadline_after(duration);
  while (zx::clock::get_monotonic() < end_time) {
    MemoryWorkloadGenerator::Workload next = workload_generator.Next();

    // Allocate memory for tests.
    zx::status<> result =
        ReallocateMemoryAsRequired(next.workload.memory_type, bytes_to_test.value(), &memory);
    if (result.is_error()) {
      status->Log("Failed to reallocate memory: %s", result.status_string());
      return false;
    }

    // Log start of test.
    status->Set("Test %4ld: CPU %2d : %s", num_tests, next.cpu, next.workload.name.c_str());

    // Switch execution to the correct CPU.
    profile_manager->SetThreadAffinity(*zx::thread::self(), 1u << next.cpu);

    // Execute the workload.
    zx::time test_start = zx::clock::get_monotonic();
    next.workload.exec(status, /*max_duration=*/(end_time - zx::clock::get_monotonic()),
                       memory.get());
    zx::duration test_duration = zx::clock::get_monotonic() - test_start;

    // Calculate test time and throughput.
    std::string throughput;
    if (next.workload.report_throughput) {
      throughput =
          fxl::StringPrintf(", throughput: %0.2f MiB/s",
                            memory->size_bytes() / DurationToSecs(test_duration) / 1024. / 1024.);
    }
    status->Log("Test %4ld: CPU %2d : %s: %0.3fs%s", num_tests, next.cpu,
                next.workload.name.c_str(), DurationToSecs(test_duration), throughput.c_str());

    num_tests++;
  }

  return true;
}

}  // namespace hwstress
