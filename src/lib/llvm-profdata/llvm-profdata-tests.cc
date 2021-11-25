// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/elfldltl/note.h>
#include <lib/elfldltl/self.h>
#include <lib/llvm-profdata/llvm-profdata.h>
#include <lib/stdcompat/span.h>

#include <cstdint>
#include <memory>

#include <gtest/gtest.h>

#include "coverage-example.h"

namespace {

// The compiler doesn't support relocatable mode on macOS.
#ifdef __APPLE__
constexpr bool kRelocatableCounters = false;
#else
constexpr bool kRelocatableCounters = true;
#endif

cpp20::span<const std::byte> MyBuildId() {
  // TODO(mcgrathr): For these unit tests, it doesn't matter what the ID is.
  // For end-to-end tests using the offline tools, this will need to be the
  // real build ID of the test module.
  static constexpr std::byte kId[] = {std::byte{0xaa}, std::byte{0xbb}};
  return kId;
}

TEST(LlvmProfdataTests, SizeBytes) {
  LlvmProfdata data;
  data.Init(MyBuildId());
  EXPECT_GT(data.size_bytes(), size_t{0});
}

TEST(LlvmProfdataTests, CountersOffsetAndSizeBytes) {
  LlvmProfdata data;
  data.Init(MyBuildId());
  EXPECT_GT(data.counters_offset(), size_t{0});
  EXPECT_GT(data.counters_size_bytes(), size_t{0});
  EXPECT_LT(data.counters_offset(), data.size_bytes());
  EXPECT_LT(data.counters_size_bytes(), data.size_bytes() - data.counters_offset());
}

TEST(LlvmProfdataTests, FixedData) {
  LlvmProfdata data;
  data.Init(MyBuildId());

  const size_t buffer_size = data.size_bytes();
  ASSERT_GT(buffer_size, size_t{0});
  std::unique_ptr<std::byte[]> buffer(new std::byte[buffer_size]);
  const cpp20::span buffer_span(buffer.get(), buffer_size);

  cpp20::span counters = data.WriteFixedData(buffer_span);
  ASSERT_FALSE(counters.empty());

  EXPECT_TRUE(data.Match(buffer_span));

  cpp20::span matched_counters = data.VerifyMatch(buffer_span);
  EXPECT_EQ(matched_counters.data(), counters.data());
  EXPECT_EQ(matched_counters.size_bytes(), counters.size_bytes());
}

TEST(LlvmProfdataTests, CopyCounters) {
  LlvmProfdata data;
  data.Init(MyBuildId());

  const size_t buffer_size = data.size_bytes();
  ASSERT_GT(buffer_size, size_t{0});
  std::unique_ptr<std::byte[]> buffer(new std::byte[buffer_size]);
  const cpp20::span buffer_span(buffer.get(), buffer_size);

  cpp20::span counters_bytes = data.WriteFixedData(buffer_span);
  ASSERT_FALSE(counters_bytes.empty());

  const cpp20::span<uint64_t> counters{
      reinterpret_cast<uint64_t*>(counters_bytes.data()),
      counters_bytes.size_bytes() / sizeof(uint64_t),
  };

  // Fill the buffer with unreasonable counter values.
  std::fill(counters.begin(), counters.end(), ~uint64_t{});

  // Now copy out the current values.
  data.CopyCounters(cpp20::as_writable_bytes(counters));

  // None of the real values should be the unreasonable value.
  for (size_t i = 0; i < counters.size(); ++i) {
    EXPECT_NE(counters[i], ~uint64_t{}) << "counter " << i;
  }

  // In case the normal profile runtime is also active, reset the bias.
  LlvmProfdata::UseLinkTimeCounters();

  // Now run some instrumented code that should be sure to touch a counter.
  RunTimeCoveredFunction();

  std::unique_ptr<uint64_t[]> new_buffer(new uint64_t[counters.size()]);
  const cpp20::span new_counters(new_buffer.get(), counters.size());

  // Fill the buffer with unreasonable counter values.
  std::fill(new_counters.begin(), new_counters.end(), ~uint64_t{});

  // Now copy out the new values after running covered code.
  data.CopyCounters(cpp20::as_writable_bytes(new_counters));

  uint64_t increase = 0;
  for (size_t i = 0; i < counters.size(); ++i) {
    // None of the real values should be the unreasonable value.
    EXPECT_NE(new_counters[i], ~uint64_t{}) << "counter " << i;

    // No counter should have decreased.
    EXPECT_GE(new_counters[i], counters[i]);

    // Accumulate all the increased hit counts together.
    increase += new_counters[i] - counters[i];
  }

  // At least one counter in RunTimeCoveredFunction should have increased.
  EXPECT_GT(increase, uint64_t{0});
}

TEST(LlvmProfdataTests, MergeCounters) {
  static constexpr uint64_t kOldCounters[] = {1, 2, 3, 4};
  uint64_t new_counters[] = {5, 6, 7, 8};
  static_assert(std::size(kOldCounters) == std::size(new_counters));

  LlvmProfdata::MergeCounters(cpp20::as_writable_bytes(cpp20::span(new_counters)),
                              cpp20::as_bytes(cpp20::span(kOldCounters)));

  EXPECT_EQ(new_counters[0], 6u);
  EXPECT_EQ(new_counters[1], 8u);
  EXPECT_EQ(new_counters[2], 10u);
  EXPECT_EQ(new_counters[3], 12u);

  LlvmProfdata data;
  data.Init(MyBuildId());

  const size_t buffer_size = data.size_bytes();
  ASSERT_GT(buffer_size, size_t{0});
  std::unique_ptr<std::byte[]> buffer(new std::byte[buffer_size]);
  const cpp20::span buffer_span(buffer.get(), buffer_size);

  cpp20::span counters_bytes = data.WriteFixedData(buffer_span);
  ASSERT_FALSE(counters_bytes.empty());

  const cpp20::span<uint64_t> counters{
      reinterpret_cast<uint64_t*>(counters_bytes.data()),
      counters_bytes.size_bytes() / sizeof(uint64_t),
  };

  // In case the normal profile runtime is also active, reset the bias.
  LlvmProfdata::UseLinkTimeCounters();

  // Run some instrumented code that should be sure to touch a counter.
  RunTimeCoveredFunction();

  // Set initial values for each counter in our buffer.
  for (size_t i = 0; i < counters.size(); ++i) {
    counters[i] = i;
  }

  // Now merge the current data into our synthetic starting data.
  data.MergeCounters(cpp20::as_writable_bytes(counters));

  uint64_t increase = 0;
  for (size_t i = 0; i < counters.size(); ++i) {
    // No counter should have decreased.
    EXPECT_GE(counters[i], i);

    // Accumulate all the increased hit counts together.
    increase += counters[i] - i;
  }

  // At least one counter in RunTimeCoveredFunction should have increased.
  EXPECT_GT(increase, uint64_t{0});
}

TEST(LlvmProfdataTests, UseCounters) {
  LlvmProfdata data;
  data.Init(MyBuildId());

  const size_t buffer_size = data.size_bytes();
  ASSERT_GT(buffer_size, size_t{0});
  std::unique_ptr<std::byte[]> buffer(new std::byte[buffer_size]);
  const cpp20::span buffer_span(buffer.get(), buffer_size);

  cpp20::span counters_bytes = data.WriteFixedData(buffer_span);
  ASSERT_FALSE(counters_bytes.empty());

  const cpp20::span<uint64_t> counters{
      reinterpret_cast<uint64_t*>(counters_bytes.data()),
      counters_bytes.size_bytes() / sizeof(uint64_t),
  };

  // Start all counters at zero.
  std::fill(counters.begin(), counters.end(), 0);

  if (kRelocatableCounters) {
    LlvmProfdata::UseCounters(counters_bytes);

    // Now run some instrumented code that should be sure to touch a counter.
    RunTimeCoveredFunction();

    // Go back to writing into the statically-allocated data.  Note that if the
    // normal profile runtime is enabled and using relocatable mode (as it
    // always does on Fuchsia), this will skew down the coverage numbers for
    // this test code itself.
    LlvmProfdata::UseLinkTimeCounters();

    uint64_t hits = 0;
    for (uint64_t count : counters) {
      hits += count;
    }

    // At least one counter in RunTimeCoveredFunction should have increased.
    EXPECT_GT(hits, uint64_t{0});
  }
}

}  // namespace
