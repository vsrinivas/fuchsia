// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/measure/time_between.h"

#include <vector>

#include "apps/tracing/lib/measure/test_events.h"
#include "gtest/gtest.h"

namespace tracing {
namespace measure {

namespace {

TEST(MeasureTimeBetweenTest, Instant) {
  std::vector<TimeBetweenSpec> specs = {
      TimeBetweenSpec({42u,
                       {"first_event", "category_foo"},
                       Anchor::Begin,
                       {"second_event", "category_foo"},
                       Anchor::Begin})};

  MeasureTimeBetween measure(std::move(specs));
  measure.Process(test::Instant("first_event", "category_foo", 1u));
  measure.Process(test::Instant("first_event", "category_foo", 2u));
  measure.Process(test::Instant("second_event", "category_foo", 4u));
  measure.Process(test::Instant("second_event", "category_foo", 10u));
  measure.Process(test::Instant("first_event", "category_foo", 12u));
  measure.Process(test::Instant("second_event", "category_foo", 13u));

  auto results = measure.results();
  EXPECT_EQ(1u, results.size());
  EXPECT_EQ(std::vector<uint64_t>({2u, 1u}), results[42u]);
}

// Verifies that we can measure the time between consecutive occurences of the
// same event.
TEST(MeasureTimeBetweenTest, SameEvent) {
  std::vector<TimeBetweenSpec> specs = {
      TimeBetweenSpec({42u,
                       {"bar", "category_foo"},
                       Anchor::Begin,
                       {"bar", "category_foo"},
                       Anchor::Begin})};

  MeasureTimeBetween measure(std::move(specs));
  measure.Process(test::Instant("bar", "category_foo", 1u));
  measure.Process(test::Instant("bar", "category_foo", 1u));
  measure.Process(test::Instant("bar", "category_foo", 4u));
  measure.Process(test::Instant("bar", "category_foo", 20u));

  auto results = measure.results();
  EXPECT_EQ(1u, results.size());
  EXPECT_EQ(std::vector<uint64_t>({0u, 3u, 16u}), results[42u]);
}

TEST(MeasureTimeBetweenTest, AnchorsEndBegin) {
  std::vector<TimeBetweenSpec> specs = {
      TimeBetweenSpec({42u,
                       {"first_event", "category_foo"},
                       Anchor::End,
                       {"second_event", "category_foo"},
                       Anchor::Begin})};

  MeasureTimeBetween measure(std::move(specs));
  measure.Process(test::DurationBegin("first_event", "category_foo", 1u));
  measure.Process(test::DurationEnd("first_event", "category_foo", 3u));
  measure.Process(test::DurationBegin("second_event", "category_foo", 4u));
  measure.Process(test::DurationEnd("second_event", "category_foo", 8u));

  auto results = measure.results();
  EXPECT_EQ(1u, results.size());
  EXPECT_EQ(std::vector<uint64_t>({1u}), results[42u]);
}

TEST(MeasureTimeBetweenTest, AnchorsBeginEnd) {
  std::vector<TimeBetweenSpec> specs = {
      TimeBetweenSpec({42u,
                       {"first_event", "category_foo"},
                       Anchor::End,
                       {"second_event", "category_foo"},
                       Anchor::Begin})};

  MeasureTimeBetween measure(std::move(specs));
  measure.Process(test::DurationBegin("first_event", "category_foo", 1u));
  measure.Process(test::DurationEnd("first_event", "category_foo", 3u));
  measure.Process(test::DurationBegin("second_event", "category_foo", 4u));
  measure.Process(test::DurationEnd("second_event", "category_foo", 8u));

  auto results = measure.results();
  EXPECT_EQ(1u, results.size());
  EXPECT_EQ(std::vector<uint64_t>({1u}), results[42u]);
}

TEST(MeasureTimeBetweenTest, AnchorsSameEvent) {
  std::vector<TimeBetweenSpec> specs = {
      TimeBetweenSpec({42u,
                       {"bar", "category_foo"},
                       Anchor::End,
                       {"bar", "category_foo"},
                       Anchor::Begin})};

  MeasureTimeBetween measure(std::move(specs));
  measure.Process(test::DurationBegin("bar", "category_foo", 1u));
  measure.Process(test::DurationEnd("bar", "category_foo", 3u));
  measure.Process(test::DurationBegin("bar", "category_foo", 4u));
  measure.Process(test::DurationEnd("bar", "category_foo", 8u));
  measure.Process(test::DurationBegin("bar", "category_foo", 16u));
  measure.Process(test::DurationEnd("bar", "category_foo", 20u));

  auto results = measure.results();
  EXPECT_EQ(1u, results.size());
  EXPECT_EQ(std::vector<uint64_t>({1u, 8u}), results[42u]);
}

}  // namespace

}  // namespace measure
}  // namespace tracing
