// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/measure/results.h"

#include "gtest/gtest.h"

namespace tracing {
namespace measure {

bool operator==(const SampleGroup& lhs, const SampleGroup& rhs) {
  return lhs.values == rhs.values && lhs.label == rhs.label;
}

bool operator==(const Result& lhs, const Result& rhs) {
  return lhs.samples == rhs.samples && lhs.unit == rhs.unit &&
         lhs.label == rhs.label;
}

namespace {

TEST(Results, Empty) {
  auto results = ComputeResults({}, {}, 1.0);
  std::vector<Result> expected = {};
  EXPECT_EQ(expected, results);
}

TEST(Results, Duration) {
  Measurements measurements;
  measurements.duration = {{42u, {"foo", "bar"}}};

  std::unordered_map<uint64_t, std::vector<trace_ticks_t>> ticks;
  ticks[42u] = {1u, 2u, 3u};

  auto results = ComputeResults(measurements, ticks, 1000.0);
  Result expected = {{{{1.0, 2.0, 3.0}, "samples 0 to 2"}}, "ms", "foo (bar)"};
  EXPECT_EQ(1u, results.size());
  EXPECT_EQ(expected, results[0]);
}

TEST(Results, ArgumentValue) {
  Measurements measurements;
  measurements.argument_value = {{42u, {"foo", "bar"}, "disk space", "MB"}};

  std::unordered_map<uint64_t, std::vector<trace_ticks_t>> ticks;
  ticks[42u] = {1u, 2u, 3u};

  auto results = ComputeResults(measurements, ticks, 1000.0);
  Result expected = {
      {{{1.0, 2.0, 3.0}, "samples 0 to 2"}}, "MB", "foo (bar), disk space"};
  EXPECT_EQ(1u, results.size());
  EXPECT_EQ(expected, results[0]);
}

TEST(Results, TimeBetween) {
  Measurements measurements;
  measurements.time_between = {{
      42u,
      {"foo1", "bar1"},
      Anchor::Begin,
      {"foo2", "bar2"},
      Anchor::Begin,
  }};

  std::unordered_map<uint64_t, std::vector<trace_ticks_t>> ticks;
  ticks[42u] = {1u, 2u, 3u};

  auto results = ComputeResults(measurements, ticks, 1000.0);
  Result expected = {{{{1.0, 2.0, 3.0}, "samples 0 to 2"}},
                     "ms",
                     "foo1 (bar1) to foo2 (bar2)"};
  EXPECT_EQ(1u, results.size());
  EXPECT_EQ(expected, results[0]);
}

TEST(Results, SplitSamples) {
  Measurements measurements;
  measurements.duration = {{42u, {"foo", "bar"}}};
  measurements.split_samples_at[42u] = {1u, 2u};

  std::unordered_map<uint64_t, std::vector<trace_ticks_t>> ticks;
  ticks[42u] = {1u, 2u, 3u, 4u};

  auto results = ComputeResults(measurements, ticks, 1000.0);
  Result expected = {{
                         {{1.0}, "samples 0 to 0"},
                         {{2.0}, "samples 1 to 1"},
                         {{3.0, 4.0}, "samples 2 to 3"},
                     },
                     "ms",
                     "foo (bar)"};
  EXPECT_EQ(1u, results.size());
  EXPECT_EQ(expected, results[0]);
}

TEST(Results, ExpectedSampleCount) {
  Measurements measurements;
  measurements.duration = {{42u, {"foo", "bar"}}};
  measurements.expected_sample_count[42u] = 3;

  std::unordered_map<uint64_t, std::vector<trace_ticks_t>> ticks;
  ticks[42u] = {1u, 2u, 3u};

  auto results = ComputeResults(measurements, ticks, 1000.0);
  Result expected = {{{{1.0, 2.0, 3.0}, "samples 0 to 2"}}, "ms", "foo (bar)"};
  EXPECT_EQ(1u, results.size());
  EXPECT_EQ(expected, results[0]);
}

TEST(Results, ExpectedSampleCountMismatch) {
  Measurements measurements;
  measurements.duration = {{42u, {"foo", "bar"}}};
  measurements.expected_sample_count[42u] = 5;

  std::unordered_map<uint64_t, std::vector<trace_ticks_t>> ticks;
  ticks[42u] = {1u, 2u, 3u};

  auto results = ComputeResults(measurements, ticks, 1000.0);
  Result expected = {{}, "ms", "foo (bar)"};
  EXPECT_EQ(1u, results.size());
  EXPECT_EQ(expected, results[0]);
}

}  // namespace

}  // namespace measure
}  // namespace tracing
