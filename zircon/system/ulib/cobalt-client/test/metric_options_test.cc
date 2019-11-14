// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <unistd.h>

#include <cobalt-client/cpp/metric-options.h>
#include <unittest/unittest.h>

namespace cobalt_client {
namespace {

bool TestLazy() {
  BEGIN_TEST;
  MetricOptions options;
  options.SetMode(MetricOptions::Mode::kLazy);
  EXPECT_TRUE(options.IsLazy());
  EXPECT_FALSE(options.IsEager());
  END_TEST;
}

bool TestEager() {
  BEGIN_TEST;
  MetricOptions options;
  options.SetMode(MetricOptions::Mode::kEager);
  EXPECT_TRUE(options.IsEager());
  EXPECT_FALSE(options.IsLazy());
  END_TEST;
}

bool TestMakeExponentialOptions() {
  BEGIN_TEST;
  HistogramOptions options = HistogramOptions::CustomizedExponential(
      /*bucket_count*/ 3, /*base*/ 4, /*scalar*/ 2, /*min*/ -10);
  ASSERT_EQ(options.base, 4);
  ASSERT_EQ(options.scalar, 2);
  ASSERT_EQ(options.offset, -12);
  ASSERT_EQ(options.type, HistogramOptions::Type::kExponential);
  ASSERT_TRUE(options.map_fn);
  ASSERT_NOT_NULL(options.map_fn);
  ASSERT_NOT_NULL(options.reverse_map_fn);
  END_TEST;
}

bool TestMakeExponentialOptionsMaxOnly() {
  BEGIN_TEST;
  HistogramOptions options = HistogramOptions::Exponential(
      /*bucket_count*/ 3, /*max*/ 14);
  ASSERT_EQ(options.base, 2);
  ASSERT_EQ(options.scalar, 2);
  ASSERT_EQ(options.offset, -2);
  // We should be greater than the predecessor of max_value,
  // which means that our max would fit in the overflow bucket.
  ASSERT_LT(nextafter(options.max_value, 0), 24);
  ASSERT_EQ(options.type, HistogramOptions::Type::kExponential);
  ASSERT_TRUE(options.map_fn);
  ASSERT_NOT_NULL(options.map_fn);
  ASSERT_NOT_NULL(options.reverse_map_fn);
  END_TEST;
}

bool TestMakeExponentialOptionsMaxOnly2() {
  BEGIN_TEST;
  HistogramOptions options = HistogramOptions::Exponential(
      /*bucket_count*/ 12, /*max*/ (4096 - 1) * 10);
  ASSERT_EQ(options.base, 2);
  // We scale by 11, instead of 10, because 12 buckets
  // can represent at mos 2^12 -1 = 4095, which would
  // leave 4096 in the overflow bucket.
  ASSERT_EQ(options.scalar, 10);
  ASSERT_EQ(options.offset, -10);
  // We should be greater than the predecessor of max_value,
  // which means that our max would fit in the overflow bucket.
  ASSERT_LT(nextafter(options.max_value, 0), 40950);
  ASSERT_EQ(options.type, HistogramOptions::Type::kExponential);
  ASSERT_TRUE(options.map_fn);
  ASSERT_NOT_NULL(options.map_fn);
  ASSERT_NOT_NULL(options.reverse_map_fn);
  END_TEST;
}

bool TestMakeExponentialOptionsMinAndMaxOnly() {
  BEGIN_TEST;
  HistogramOptions options = HistogramOptions::Exponential(
      /*bucket_count*/ 3, /*min*/ 10, /*max*/ 24);
  ASSERT_EQ(options.base, 2);
  ASSERT_EQ(options.scalar, 2);
  ASSERT_EQ(options.offset, 8);
  ASSERT_LT(nextafter(options.max_value, 0), 24);
  ASSERT_EQ(options.type, HistogramOptions::Type::kExponential);
  ASSERT_TRUE(options.map_fn);
  ASSERT_NOT_NULL(options.map_fn);
  ASSERT_NOT_NULL(options.reverse_map_fn);
  END_TEST;
}

bool TestMakeExponentialOptionsMaxContainedInLastBucket() {
  BEGIN_TEST;
  HistogramOptions options = HistogramOptions::Exponential(
      /*bucket_count*/ 3, /*min*/ 10, /*max*/ 23);
  ASSERT_EQ(options.base, 2);
  ASSERT_EQ(options.scalar, 2);
  ASSERT_EQ(options.offset, 8);
  // |max_value| should be greater than our max, which means that
  // our max fits in the last non overflow bucket.
  ASSERT_GT(options.max_value, 23);
  ASSERT_EQ(options.type, HistogramOptions::Type::kExponential);
  ASSERT_TRUE(options.map_fn);
  ASSERT_NOT_NULL(options.map_fn);
  ASSERT_NOT_NULL(options.reverse_map_fn);
  END_TEST;
}

bool TestExponentialInvalidBase() {
  BEGIN_TEST;
  HistogramOptions options = HistogramOptions::CustomizedExponential(
      /*bucket_count*/ 1, /*base*/ 0, /*scalar*/ 2, /*min*/ -10);
  ASSERT_FALSE(options.IsValid());
  END_TEST;
}

bool TestExponentialInvalidScalar() {
  BEGIN_TEST;
  HistogramOptions options = HistogramOptions::CustomizedExponential(
      /*bucket_count*/ 1, /*base*/ 1, /*scalar*/ 0, /*min*/ -10);
  ASSERT_FALSE(options.IsValid());
  END_TEST;
}

// Verify correct buckect assignment along the boundaries and points within the bucket for
// exponential bucket width.
bool TestExponentialMap() {
  BEGIN_TEST;
  // This generates the following histogram:
  //   |      | |  |        |         |
  // -inf     5 8  14       26      +inf
  HistogramOptions options = HistogramOptions::CustomizedExponential(
      /*bucket_count*/ 3, /*base*/ 2, /*scalar*/ 3, /*min*/ 5);
  // Bucket count differs in 2, due to underflow and overflow additional buckets.
  EXPECT_EQ(options.map_fn(/*value*/ 4, /*bucket_count*/ 5, options), 0);
  EXPECT_EQ(options.map_fn(nextafter(5, 4), 5, options), 0);
  EXPECT_EQ(options.map_fn(5, 5, options), 1);
  EXPECT_EQ(options.map_fn(7.5, 5, options), 1);
  EXPECT_EQ(options.map_fn(nextafter(8, 7), 5, options), 1);
  EXPECT_EQ(options.map_fn(8, 5, options), 2);
  EXPECT_EQ(options.map_fn(12, 5, options), 2);
  EXPECT_EQ(options.map_fn(nextafter(12, 11), 5, options), 2);
  EXPECT_EQ(options.map_fn(14, 5, options), 3);
  EXPECT_EQ(options.map_fn(18, 5, options), 3);
  EXPECT_EQ(options.map_fn(nextafter(26, 25), 5, options), 3);
  EXPECT_EQ(options.map_fn(26, 5, options), 4);
  END_TEST;
}

bool TestExponentialReverseMap() {
  BEGIN_TEST;
  // This generates the following histogram:
  //   |      | |  |        |         |
  // -inf     5 8  14       26      +inf
  HistogramOptions options = HistogramOptions::CustomizedExponential(
      /*bucket_count*/ 3, /*base*/ 2, /*scalar*/ 3, /*min*/ 5);
  EXPECT_EQ(options.reverse_map_fn(/*bucket_index*/ 0, 3, options), -DBL_MAX);
  // Bucket count differs in 2, due to underflow and overflow additional buckets.
  EXPECT_EQ(options.reverse_map_fn(1, 5, options), 5);
  EXPECT_EQ(options.reverse_map_fn(2, 5, options), 8);
  EXPECT_EQ(options.reverse_map_fn(3, 5, options), 14);
  EXPECT_EQ(options.reverse_map_fn(4, 5, options), 26);
  END_TEST;
}

bool TestMakeLinearOptions() {
  BEGIN_TEST;
  HistogramOptions options =
      HistogramOptions::CustomizedLinear(/*bucket_count*/ 3, /*scalar*/ 2, /*min*/ -10);
  ASSERT_EQ(options.scalar, 2);
  ASSERT_EQ(options.offset, -10);
  ASSERT_EQ(options.type, HistogramOptions::Type::kLinear);
  ASSERT_NOT_NULL(options.map_fn);
  ASSERT_NOT_NULL(options.reverse_map_fn);
  ASSERT_TRUE(options.IsValid());
  END_TEST;
}

bool TestMakeLinearOptionsMaxOnly() {
  BEGIN_TEST;
  HistogramOptions options = HistogramOptions::Linear(/*bucket_count*/ 3, /*max*/ 15);
  ASSERT_EQ(options.scalar, 5);
  ASSERT_EQ(options.offset, 0);
  ASSERT_EQ(options.type, HistogramOptions::Type::kLinear);
  ASSERT_TRUE(options.map_fn);
  ASSERT_NOT_NULL(options.map_fn);
  ASSERT_NOT_NULL(options.reverse_map_fn);
  END_TEST;
}

bool TestMakeLinearOptionsMinAndMaxOnly() {
  BEGIN_TEST;
  HistogramOptions options = HistogramOptions::Linear(/*bucket_count*/ 3, /*min*/ 9, /*max*/ 15);
  ASSERT_EQ(options.scalar, 2);
  ASSERT_EQ(options.offset, 9);
  ASSERT_EQ(options.type, HistogramOptions::Type::kLinear);
  ASSERT_TRUE(options.map_fn);
  ASSERT_NOT_NULL(options.map_fn);
  ASSERT_NOT_NULL(options.reverse_map_fn);
  END_TEST;
}

bool TestMakeLinearOptionsMaxContainedInLastBucket() {
  BEGIN_TEST;
  HistogramOptions options = HistogramOptions::Linear(/*bucket_count*/ 3, /*min*/ 9, /*max*/ 14);
  ASSERT_EQ(options.scalar, 2);
  ASSERT_EQ(options.offset, 9);
  ASSERT_GT(options.max_value, 14);
  ASSERT_EQ(options.type, HistogramOptions::Type::kLinear);
  ASSERT_TRUE(options.map_fn);
  ASSERT_NOT_NULL(options.map_fn);
  ASSERT_NOT_NULL(options.reverse_map_fn);
  END_TEST;
}

bool TestLinearInvalidScalar() {
  BEGIN_TEST;
  HistogramOptions options =
      HistogramOptions::CustomizedLinear(/*bucket_count*/ 1, /*scalar*/ 0, /*min*/ -10);
  ASSERT_FALSE(options.IsValid());
  END_TEST;
}

// Verify correct bucket assignment along the boundaries and points within the bucket for
// linear bucket width.
bool TestLinearMap() {
  BEGIN_TEST;
  // This generates the following histogram:
  //   |      |    |   |    |         |
  // -inf    -10  -8  -6   -4        +inf
  HistogramOptions options =
      HistogramOptions::CustomizedLinear(/*bucket_count*/ 3, /*scalar*/ 2, /*min*/ -10);
  // bucket count differs in 2 due to underflow and overflow additional buckets.
  EXPECT_EQ(options.map_fn(/*value*/ -15, 5, options), 0);
  EXPECT_EQ(options.map_fn(nextafter(-10.0, -11), 5, options), 0);
  EXPECT_EQ(options.map_fn(-10.0, 5, options), 1);
  EXPECT_EQ(options.map_fn(-9.0, 5, options), 1);
  EXPECT_EQ(options.map_fn(-8.0, 5, options), 2);
  EXPECT_EQ(options.map_fn(-7.0, 5, options), 2);
  EXPECT_EQ(options.map_fn(-6.0, 5, options), 3);
  EXPECT_EQ(options.map_fn(-5.0, 5, options), 3);
  EXPECT_EQ(options.map_fn(nexttoward(-4.0, -5.0), 5, options), 3);
  EXPECT_EQ(options.map_fn(-4.0, 5, options), 4);
  END_TEST;
}

bool TestLinearReverseMap() {
  BEGIN_TEST;
  // This generates the following histogram:
  //   |      |    |   |    |         |
  // -inf    -10  -8  -6   -4        +inf
  HistogramOptions options =
      HistogramOptions::CustomizedLinear(/*bucket_count*/ 3, /*scalar*/ 2, /*min*/ -10);
  // bucket count differs in 2 due to underflow and overflow additional buckets.
  EXPECT_EQ(options.reverse_map_fn(/*bucket_index*/ 0, 3, options), -DBL_MAX);
  EXPECT_EQ(options.reverse_map_fn(1, 5, options), -10);
  EXPECT_EQ(options.reverse_map_fn(2, 5, options), -8);
  EXPECT_EQ(options.reverse_map_fn(3, 5, options), -6);
  EXPECT_EQ(options.reverse_map_fn(4, 5, options), -4);
  END_TEST;
}

BEGIN_TEST_CASE(MetricOptionsTest)
RUN_TEST(TestEager)
RUN_TEST(TestLazy)
END_TEST_CASE(MetricOptionsTest)

BEGIN_TEST_CASE(HistogramOptionsTest)
RUN_TEST(TestMakeExponentialOptions)
RUN_TEST(TestMakeExponentialOptionsMaxOnly)
RUN_TEST(TestMakeExponentialOptionsMaxOnly2)
RUN_TEST(TestMakeExponentialOptionsMinAndMaxOnly)
RUN_TEST(TestMakeExponentialOptionsMaxContainedInLastBucket)
RUN_TEST(TestExponentialInvalidBase)
RUN_TEST(TestExponentialInvalidScalar)
RUN_TEST(TestExponentialMap)
RUN_TEST(TestExponentialReverseMap)
RUN_TEST(TestMakeLinearOptions)
RUN_TEST(TestMakeLinearOptionsMaxOnly)
RUN_TEST(TestMakeLinearOptionsMinAndMaxOnly)
RUN_TEST(TestMakeLinearOptionsMaxContainedInLastBucket)
RUN_TEST(TestLinearInvalidScalar)
RUN_TEST(TestLinearMap)
RUN_TEST(TestLinearReverseMap)
END_TEST_CASE(HistogramOptionsTest)
}  // namespace
}  // namespace cobalt_client
