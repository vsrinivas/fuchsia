// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/measure/duration.h"

#include <string>
#include <vector>

#include "apps/tracing/lib/measure/test_events.h"
#include "gtest/gtest.h"

namespace tracing {
namespace measure {
namespace {

TEST(MeasureDurationTest, Duration) {
  std::vector<DurationSpec> specs = {
      DurationSpec({42u, {"event_foo", "category_bar"}})};

  MeasureDuration measure(std::move(specs));
  // Add a matching begin event.
  measure.Process(test::DurationBegin("event_foo", "category_bar", 10u));

  // Add a not-matching begin event that should be ignored.
  measure.Process(test::DurationBegin("something_else", "category_bar", 12u));

  // Add a not-matching end event that should be ignored.
  measure.Process(test::DurationEnd("something_else", "category_bar", 14u));

  // Add a matching end event.
  measure.Process(test::DurationEnd("event_foo", "category_bar", 16u));

  auto results = measure.results();
  EXPECT_EQ(1u, results.size());
  EXPECT_EQ(std::vector<uint64_t>({6u}), results[42u]);
}

TEST(MeasureDurationTest, DurationEmpty) {
  std::vector<DurationSpec> specs = {
      DurationSpec({42u, {"event_foo", "category_bar"}})};

  MeasureDuration measure(std::move(specs));
  auto results = measure.results();
  EXPECT_EQ(0u, results.size());
}

// Verifies that two measurements can target the same trace event.
TEST(MeasureDurationTest, DurationTwoMeasurements) {
  std::vector<DurationSpec> specs = {
      DurationSpec({42u, {"event_foo", "category_bar"}}),
      DurationSpec({43u, {"event_foo", "category_bar"}})};

  MeasureDuration measure(std::move(specs));
  measure.Process(test::DurationBegin("event_foo", "category_bar", 10u));
  measure.Process(test::DurationEnd("event_foo", "category_bar", 16u));

  auto results = measure.results();
  EXPECT_EQ(2u, results.size());
  EXPECT_EQ(std::vector<uint64_t>({6u}), results[42u]);
  EXPECT_EQ(std::vector<uint64_t>({6u}), results[43u]);
}

TEST(MeasureDurationTest, DurationNested) {
  std::vector<DurationSpec> specs = {
      DurationSpec({42u, {"event_foo", "category_bar"}})};

  MeasureDuration measure(std::move(specs));
  // Add a matching begin event.
  measure.Process(test::DurationBegin("event_foo", "category_bar", 10u));

  // Add another matching begin event.
  measure.Process(test::DurationBegin("event_foo", "category_bar", 12u));

  // Add a matching end event.
  measure.Process(test::DurationEnd("event_foo", "category_bar", 14u));

  // Add a matching end event.
  measure.Process(test::DurationEnd("event_foo", "category_bar", 16u));

  auto results = measure.results();
  EXPECT_EQ(1u, results.size());
  EXPECT_EQ(std::vector<uint64_t>({2u, 6u}), results[42u]);
}

TEST(MeasureDurationTest, Async) {
  std::vector<DurationSpec> specs = {
      DurationSpec({42u, {"event_foo", "category_bar"}})};

  MeasureDuration measure(std::move(specs));
  // Add a begin event of id 1u.
  measure.Process(test::AsyncBegin(1u, "event_foo", "category_bar", 10u));

  // Add a begin event of id 2u.
  measure.Process(test::AsyncBegin(2u, "event_foo", "category_bar", 12u));

  // Add an end event for id 1u.
  measure.Process(test::AsyncEnd(1u, "event_foo", "category_bar", 14u));

  // Add an end event for id 2u.
  measure.Process(test::AsyncEnd(2u, "event_foo", "category_bar", 16u));

  auto results = measure.results();
  EXPECT_EQ(1u, results.size());
  EXPECT_EQ(std::vector<uint64_t>({4u, 4u}), results[42u]);
}

TEST(MeasureDurationTest, DurationAsyncTwoMeasurements) {
  std::vector<DurationSpec> specs = {
      DurationSpec({42u, {"event_foo", "category_bar"}}),
      DurationSpec({43u, {"event_foo", "category_bar"}})};

  MeasureDuration measure(std::move(specs));
  // Add a begin event of id 1u.
  measure.Process(test::AsyncBegin(1u, "event_foo", "category_bar", 10u));

  // Add an end event for id 1u.
  measure.Process(test::AsyncEnd(1u, "event_foo", "category_bar", 14u));

  auto results = measure.results();
  EXPECT_EQ(2u, results.size());
  EXPECT_EQ(std::vector<uint64_t>({4u}), results[42u]);
  EXPECT_EQ(std::vector<uint64_t>({4u}), results[43u]);
}

TEST(MeasureDurationTest, EventMatchingByNameAndCategory) {
  std::vector<DurationSpec> specs = {
      DurationSpec({40u, {"event_foo", "category_bar"}}),
      DurationSpec({41u, {"event_foo", "category_bazinga"}}),
      DurationSpec({42u, {"event_abc", "category_bazinga"}}),
      DurationSpec({43u, {"event_abc", "category_bar"}})};

  MeasureDuration measure(std::move(specs));
  measure.Process(test::DurationBegin("event_foo", "category_bar", 0u));
  measure.Process(test::DurationEnd("event_foo", "category_bar", 1u));

  measure.Process(test::DurationBegin("event_foo", "category_bazinga", 0u));
  measure.Process(test::DurationEnd("event_foo", "category_bazinga", 2u));

  measure.Process(test::DurationBegin("event_abc", "category_bazinga", 0u));
  measure.Process(test::DurationEnd("event_abc", "category_bazinga", 3u));

  measure.Process(test::DurationBegin("event_abc", "category_bar", 0u));
  measure.Process(test::DurationEnd("event_abc", "category_bar", 4u));

  auto results = measure.results();
  EXPECT_EQ(4u, results.size());
  EXPECT_EQ(std::vector<uint64_t>({1u}), results[40u]);
  EXPECT_EQ(std::vector<uint64_t>({2u}), results[41u]);
  EXPECT_EQ(std::vector<uint64_t>({3u}), results[42u]);
  EXPECT_EQ(std::vector<uint64_t>({4u}), results[43u]);
}

TEST(MeasureDurationTest, AsyncNested) {
  std::vector<DurationSpec> specs = {
      DurationSpec({42u, {"event_foo", "category_bar"}}),
      DurationSpec({43u, {"event_baz", "category_bar"}})};

  MeasureDuration measure(std::move(specs));
  // Add a begin event for event_foo of id 0u.
  measure.Process(test::AsyncBegin(0u, "event_foo", "category_bar", 10u));

  // Add a begin event for event_baz of id 0u.
  measure.Process(test::AsyncBegin(0u, "event_baz", "category_bar", 12u));

  // Add an end event for id 1u.
  measure.Process(test::AsyncEnd(0u, "event_foo", "category_bar", 14u));

  // Add an end event for id 2u.
  measure.Process(test::AsyncEnd(0u, "event_baz", "category_bar", 16u));

  auto results = measure.results();
  EXPECT_EQ(2u, results.size());
  EXPECT_EQ(std::vector<uint64_t>({4u}), results[42u]);
  EXPECT_EQ(std::vector<uint64_t>({4u}), results[43u]);
}

}  // namespace

}  // namespace measure
}  // namespace tracing
