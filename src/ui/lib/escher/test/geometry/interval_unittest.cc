// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/geometry/interval.h"

#include <gtest/gtest.h>

#include "src/lib/fxl/logging.h"
#include "src/ui/lib/escher/geometry/intersection.h"
#include "src/ui/lib/escher/geometry/types.h"

namespace {

using namespace escher;

TEST(Interval, Construct) {
  Interval interval;  // empty.
  Interval interval2(30, 50);

  EXPECT_TRUE(interval.is_empty());
  EXPECT_EQ(interval2.min(), 30);
  EXPECT_EQ(interval2.max(), 50);
}

TEST(Interval, Join) {
  Interval interval_1(10, 20);
  Interval interval_2(50, 60);

  Interval result = interval_1.Join(interval_2);
  EXPECT_TRUE(result == Interval(10, 60));
}

TEST(Interval, Intersect) {
  Interval empty;
  Interval first(10, 20);
  Interval second(12, 19);
  Interval third(30, 40);
  Interval fourth(35, 45);

  EXPECT_TRUE(empty.Intersect(first) == Interval());
  EXPECT_TRUE(first.Intersect(empty) == Interval());

  EXPECT_TRUE(first.Intersect(second) == second);
  EXPECT_TRUE(second.Intersect(first) == second);

  EXPECT_TRUE(first.Intersect(third) == Interval());
  EXPECT_TRUE(third.Intersect(first) == Interval());

  // Partially intersecting intervals.
  EXPECT_TRUE(third.Intersect(fourth) == Interval(35, 40));
  EXPECT_TRUE(fourth.Intersect(third) == Interval(35, 40));
}

TEST(Interval, Contains) {
  Interval interval(0, 100);
  Interval interval2(0, 90);
  Interval interval3(50, 70);
  Interval interval4(90, 110);

  EXPECT_TRUE(interval.Contains(35));
  EXPECT_FALSE(interval.Contains(200));

  // Checks to make sure intervals are closed.
  EXPECT_TRUE(interval.Contains(interval2));

  EXPECT_TRUE(interval.Contains(interval3));
  EXPECT_TRUE(interval2.Contains(interval3));

  EXPECT_FALSE(interval.Contains(interval4));
}

}  // namespace
