// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/measure/results.h"

#include "gtest/gtest.h"

namespace tracing {
namespace measure {

bool operator==(const Result& lhs, const Result& rhs) {
  return lhs.values == rhs.values && lhs.unit == rhs.unit && lhs.label == rhs.label &&
         lhs.test_suite == rhs.test_suite && lhs.split_first == rhs.split_first;
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
  Result expected = {{1.0, 2.0, 3.0}, "ms", "foo (bar)", "", false};
  EXPECT_EQ(1u, results.size());
  EXPECT_EQ(expected, results[0]);
}

TEST(Results, ArgumentValue) {
  Measurements measurements;
  measurements.argument_value = {{42u, {"foo", "bar"}, "disk space", "MB"}};

  std::unordered_map<uint64_t, std::vector<trace_ticks_t>> ticks;
  ticks[42u] = {1u, 2u, 3u};

  auto results = ComputeResults(measurements, ticks, 1000.0);
  Result expected = {{1.0, 2.0, 3.0}, "MB", "foo (bar), disk space", "", false};
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
  Result expected = {{1.0, 2.0, 3.0}, "ms", "foo1 (bar1) to foo2 (bar2)", "", false};
  EXPECT_EQ(1u, results.size());
  EXPECT_EQ(expected, results[0]);
}

TEST(Results, SplitFirst) {
  Measurements measurements;
  measurements.duration = {{42u, {"foo", "bar"}}};
  measurements.duration[0].common.split_first = true;

  std::unordered_map<uint64_t, std::vector<trace_ticks_t>> ticks;
  ticks[42u] = {1u, 2u, 3u};

  auto results = ComputeResults(measurements, ticks, 1000.0);
  Result expected = {{1.0, 2.0, 3.0}, "ms", "foo (bar)", "", true};
  EXPECT_EQ(1u, results.size());
  EXPECT_EQ(expected, results[0]);
}

TEST(Results, ExpectedSampleCount) {
  Measurements measurements;
  measurements.duration = {{42u, {"foo", "bar"}}};
  measurements.duration[0].common.expected_sample_count = 3;

  std::unordered_map<uint64_t, std::vector<trace_ticks_t>> ticks;
  ticks[42u] = {1u, 2u, 3u};

  auto results = ComputeResults(measurements, ticks, 1000.0);
  Result expected = {{1.0, 2.0, 3.0}, "ms", "foo (bar)", "", false};
  EXPECT_EQ(1u, results.size());
  EXPECT_EQ(expected, results[0]);
}

TEST(Results, ExpectedSampleCountMismatch) {
  Measurements measurements;
  measurements.duration = {{42u, {"foo", "bar"}}};
  measurements.duration[0].common.expected_sample_count = 5;

  std::unordered_map<uint64_t, std::vector<trace_ticks_t>> ticks;
  ticks[42u] = {1u, 2u, 3u};

  auto results = ComputeResults(measurements, ticks, 1000.0);
  Result expected = {{}, "ms", "foo (bar)", "", false};
  EXPECT_EQ(1u, results.size());
  EXPECT_EQ(expected, results[0]);
}

}  // namespace

}  // namespace measure
}  // namespace tracing
