// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cobalt-client/cpp/metric_options.h>

#include <unistd.h>

#include <cfloat>
#include <cmath>
#include <cstdint>

#include <zxtest/zxtest.h>

namespace cobalt_client {
namespace {

constexpr std::string_view kComponent = "SomeComponent";
constexpr uint32_t kMetricId = 1;
constexpr std::array<uint32_t, MetricOptions::kMaxEventCodes> kEventCodes = {0, 1, 2, 3, 4};

MetricOptions MakeMetricOptions(
    uint32_t metric_id, std::string_view component,
    const std::array<uint32_t, MetricOptions::kMaxEventCodes>& event_codes) {
  MetricOptions info = {};
  info.metric_id = metric_id;
  info.component = component;
  info.event_codes = event_codes;
  return info;
}

TEST(MetricOptionsTest, EqualOperatorIdentity) {
  MetricOptions info = MakeMetricOptions(kMetricId, kComponent, kEventCodes);

  ASSERT_TRUE(info == info);
  ASSERT_FALSE(info != info);
}

TEST(MetricOptionsTest, EqualOperatorSameValue) {
  MetricOptions info_1 = MakeMetricOptions(kMetricId, kComponent, kEventCodes);
  MetricOptions info_2 = MakeMetricOptions(kMetricId, kComponent, kEventCodes);

  ASSERT_TRUE(info_1 == info_2);
  ASSERT_FALSE(info_1 != info_2);
}

TEST(MetricOptionsTest, EqualOperatorForDifferentValues) {
  MetricOptions info_1 = MakeMetricOptions(kMetricId, kComponent, kEventCodes);
  MetricOptions info_2 = MakeMetricOptions(kMetricId + 1, kComponent, kEventCodes);

  ASSERT_FALSE(info_1 == info_2);
  ASSERT_TRUE(info_1 != info_2);
}

TEST(MetricOptionsTest, LessThanIsFalseForEqualInfos) {
  MetricOptions::LessThan less_than;
  MetricOptions info = MakeMetricOptions(kMetricId, kComponent, kEventCodes);

  ASSERT_FALSE(less_than(info, info));
}

TEST(MetricOptionsTest, LessThanIsLexicographicWithMetricId) {
  MetricOptions::LessThan less_than;
  MetricOptions info_1 = MakeMetricOptions(kMetricId, kComponent, kEventCodes);
  MetricOptions info_2 = MakeMetricOptions(kMetricId + 1, kComponent, kEventCodes);

  ASSERT_TRUE(less_than(info_1, info_2));
  ASSERT_FALSE(less_than(info_2, info_1));
}

TEST(MetricOptionsTest, LessThanIsLexicographicWithEventCodes) {
  MetricOptions::LessThan less_than;
  MetricOptions info_1 = MakeMetricOptions(kMetricId, kComponent, {0, 1, 2, 3, 4});
  MetricOptions info_2 = MakeMetricOptions(kMetricId, kComponent, {0, 1, 2, 3, 5});

  ASSERT_TRUE(less_than(info_1, info_2));
  ASSERT_FALSE(less_than(info_2, info_1));
}

TEST(MetricOptionsTest, LessThanIsLexicographicWithComponents) {
  MetricOptions::LessThan less_than;
  MetricOptions info_1 = MakeMetricOptions(kMetricId, "aaa", kEventCodes);
  MetricOptions info_2 = MakeMetricOptions(kMetricId, "aab", kEventCodes);

  ASSERT_TRUE(less_than(info_1, info_2));
  ASSERT_FALSE(less_than(info_2, info_1));
}

TEST(HistogramOptionsTest, CustomizedExponentialParamsSetParametersCorrectly) {
  HistogramOptions options = HistogramOptions::CustomizedExponential(
      /*bucket_count*/ 3, /*base*/ 4, /*scalar*/ 2, /*min*/ -10);
  ASSERT_EQ(4, options.base);
  ASSERT_EQ(2, options.scalar);
  ASSERT_EQ(-12, options.offset);
  ASSERT_EQ(HistogramOptions::Type::kExponential, options.type);
  ASSERT_TRUE(options.map_fn);
  ASSERT_NOT_NULL(options.map_fn);
  ASSERT_NOT_NULL(options.reverse_map_fn);
}

TEST(HistogramOptionsTest, ExponentialWithMaxOnlySetsParametersCorrectly) {
  {
    // The max falls in last non overflow bucket.
    HistogramOptions options = HistogramOptions::Exponential(
        /*bucket_count*/ 3, /*max*/ 13);
    ASSERT_EQ(2, options.base);
    ASSERT_EQ(2, options.scalar);
    ASSERT_EQ(-2, options.offset);
    ASSERT_LT(13, options.max_value);
    ASSERT_EQ(HistogramOptions::Type::kExponential, options.type);
    ASSERT_TRUE(options.map_fn);
    ASSERT_NOT_NULL(options.map_fn);
    ASSERT_NOT_NULL(options.reverse_map_fn);
  }
  {
    // The max falls in overflow bucket.
    HistogramOptions options = HistogramOptions::Exponential(
        /*bucket_count*/ 3, /*max*/ 14);
    ASSERT_EQ(2, options.base);
    ASSERT_EQ(2, options.scalar);
    ASSERT_EQ(-2, options.offset);
    ASSERT_GE(14, nextafter(options.max_value, 0));
    ASSERT_EQ(HistogramOptions::Type::kExponential, options.type);
    ASSERT_TRUE(options.map_fn);
    ASSERT_NOT_NULL(options.map_fn);
    ASSERT_NOT_NULL(options.reverse_map_fn);
  }
  {
    // max falls in overflow bucket.
    HistogramOptions options = HistogramOptions::Exponential(
        /*bucket_count*/ 12, /*max*/ (4096 - 1) * 10);
    ASSERT_EQ(2, options.base);
    ASSERT_EQ(10, options.scalar);
    ASSERT_EQ(-10, options.offset);
    ASSERT_GE(40950, nextafter(options.max_value, 0));
    ASSERT_EQ(HistogramOptions::Type::kExponential, options.type);
    ASSERT_TRUE(options.map_fn);
    ASSERT_NOT_NULL(options.map_fn);
    ASSERT_NOT_NULL(options.reverse_map_fn);
  }
}

TEST(HistogramOptionsTest, ExponentialWithMaxAndMinSetsParamtersCorrectly) {
  HistogramOptions options = HistogramOptions::Exponential(
      /*bucket_count*/ 3, /*min*/ 10, /*max*/ 24);
  ASSERT_EQ(2, options.base);
  ASSERT_EQ(2, options.scalar);
  ASSERT_EQ(8, options.offset);
  ASSERT_LT(nextafter(options.max_value, 0), 24);
  ASSERT_EQ(HistogramOptions::Type::kExponential, options.type);
  ASSERT_TRUE(options.map_fn);
  ASSERT_NOT_NULL(options.map_fn);
  ASSERT_NOT_NULL(options.reverse_map_fn);
}

TEST(HistogramOptionsTest, ExponentialMaxValueIsContainedLastNonOverflowBucket) {
  HistogramOptions options = HistogramOptions::Exponential(
      /*bucket_count*/ 3, /*min*/ 10, /*max*/ 23);
  ASSERT_EQ(2, options.base);
  ASSERT_EQ(2, options.scalar);
  ASSERT_EQ(8, options.offset);
  // |max_value| should be greater than our max, which means that
  // our max fits in the last non overflow bucket.
  ASSERT_LE(23, options.max_value);
  ASSERT_EQ(HistogramOptions::Type::kExponential, options.type);
  ASSERT_TRUE(options.map_fn);
  ASSERT_NOT_NULL(options.map_fn);
  ASSERT_NOT_NULL(options.reverse_map_fn);
}

TEST(HistogramOptionsTest, ExponentialWithInvalidBaseIsNotValid) {
  HistogramOptions options = HistogramOptions::CustomizedExponential(
      /*bucket_count*/ 1, /*base*/ 1, /*scalar*/ 0, /*min*/ -10);
  ASSERT_FALSE(options.IsValid());
}

TEST(HistogramOptionsTest, ExponentialMapFunctionMapsToRightBucket) {
  // This generates the following histogram:
  //   |      | |  |        |         |
  // -inf     5 8  14       26      +inf
  HistogramOptions options = HistogramOptions::CustomizedExponential(
      /*bucket_count*/ 3, /*base*/ 2, /*scalar*/ 3, /*min*/ 5);
  // Bucket count differs in 2, due to underflow and overflow additional buckets.
  EXPECT_EQ(0, options.map_fn(/*value*/ 4, /*bucket_count*/ 5, options));
  EXPECT_EQ(0, options.map_fn(nextafter(5, 4), 5, options));
  EXPECT_EQ(1, options.map_fn(5, 5, options));
  EXPECT_EQ(1, options.map_fn(7.5, 5, options));
  EXPECT_EQ(1, options.map_fn(nextafter(8, 7), 5, options));
  EXPECT_EQ(2, options.map_fn(8, 5, options));
  EXPECT_EQ(2, options.map_fn(12, 5, options));
  EXPECT_EQ(2, options.map_fn(nextafter(12, 11), 5, options));
  EXPECT_EQ(3, options.map_fn(14, 5, options));
  EXPECT_EQ(3, options.map_fn(18, 5, options));
  EXPECT_EQ(3, options.map_fn(nextafter(26, 25), 5, options));
  EXPECT_EQ(4, options.map_fn(26, 5, options));
}

TEST(HistogramOptionsTest, ExponentialReverseMapMaptsToLowerBound) {
  // This generates the following histogram:
  //   |      | |  |        |         |
  // -inf     5 8  14       26      +inf
  HistogramOptions options = HistogramOptions::CustomizedExponential(
      /*bucket_count*/ 3, /*base*/ 2, /*scalar*/ 3, /*min*/ 5);
  EXPECT_EQ(-std::numeric_limits<double>::max(),
            options.reverse_map_fn(/*bucket_index*/ 0, 3, options));
  // Bucket count differs in 2, due to underflow and overflow additional buckets.
  EXPECT_EQ(5, options.reverse_map_fn(1, 5, options));
  EXPECT_EQ(8, options.reverse_map_fn(2, 5, options));
  EXPECT_EQ(14, options.reverse_map_fn(3, 5, options));
  EXPECT_EQ(26, options.reverse_map_fn(4, 5, options));
}

TEST(HistogramOptionsTest, CustomizedLinearSetsParametersCorrectly) {
  HistogramOptions options =
      HistogramOptions::CustomizedLinear(/*bucket_count*/ 3, /*scalar*/ 2, /*min*/ -10);
  ASSERT_EQ(2, options.scalar);
  ASSERT_EQ(-10, options.offset);
  ASSERT_LE(-4, options.max_value);
  ASSERT_EQ(HistogramOptions::Type::kLinear, options.type);
  ASSERT_NOT_NULL(options.map_fn);
  ASSERT_NOT_NULL(options.reverse_map_fn);
  ASSERT_TRUE(options.IsValid());
}

TEST(HistogramOptionsTest, LinearWithMaxSetsParametersCorrectly) {
  HistogramOptions options = HistogramOptions::Linear(/*bucket_count*/ 3, /*max*/ 15);
  ASSERT_EQ(5, options.scalar);
  ASSERT_EQ(0, options.offset);
  ASSERT_LE(15, options.max_value);
  ASSERT_EQ(HistogramOptions::Type::kLinear, options.type);
  ASSERT_NOT_NULL(options.map_fn);
  ASSERT_NOT_NULL(options.reverse_map_fn);
  ASSERT_TRUE(options.IsValid());
}

TEST(HistogramOptionsTest, LinearWithMinAndMaxSetsParametersCorrectly) {
  HistogramOptions options = HistogramOptions::Linear(/*bucket_count*/ 3, /*min*/ 9, /*max*/ 15);
  ASSERT_EQ(2, options.scalar);
  ASSERT_EQ(9, options.offset);
  ASSERT_LE(15, options.max_value);
  ASSERT_EQ(HistogramOptions::Type::kLinear, options.type);
  ASSERT_NOT_NULL(options.map_fn);
  ASSERT_NOT_NULL(options.reverse_map_fn);
  ASSERT_TRUE(options.IsValid());
}

TEST(HistogramOptionsTest, LinearMaxValueContainedInLastBucket) {
  HistogramOptions options = HistogramOptions::Linear(/*bucket_count*/ 3, /*min*/ 9, /*max*/ 14);
  ASSERT_EQ(2, options.scalar);
  ASSERT_EQ(9, options.offset);
  ASSERT_LE(14, options.max_value);
  ASSERT_EQ(HistogramOptions::Type::kLinear, options.type);
  ASSERT_NOT_NULL(options.map_fn);
  ASSERT_NOT_NULL(options.reverse_map_fn);
  ASSERT_TRUE(options.IsValid());
}

TEST(HistogramOptionsTest, LinearWithInvalidScalarIsInvalid) {
  HistogramOptions options =
      HistogramOptions::CustomizedLinear(/*bucket_count*/ 1, /*scalar*/ 0, /*min*/ -10);
  ASSERT_FALSE(options.IsValid());
}

TEST(HistogramOptionsTest, LinearMapFunctionMapsToCorrectBucket) {
  // This generates the following histogram:
  //   |      |    |   |    |         |
  // -inf    -10  -8  -6   -4        +inf
  HistogramOptions options =
      HistogramOptions::CustomizedLinear(/*bucket_count*/ 3, /*scalar*/ 2, /*min*/ -10);
  // bucket count differs in 2 due to underflow and overflow additional buckets.
  EXPECT_EQ(0, options.map_fn(/*value*/ -15, 5, options));
  EXPECT_EQ(0, options.map_fn(nextafter(-10.0, -11), 5, options));
  EXPECT_EQ(1, options.map_fn(-10.0, 5, options));
  EXPECT_EQ(1, options.map_fn(-9.0, 5, options));
  EXPECT_EQ(2, options.map_fn(-8.0, 5, options));
  EXPECT_EQ(2, options.map_fn(-7.0, 5, options));
  EXPECT_EQ(3, options.map_fn(-6.0, 5, options));
  EXPECT_EQ(3, options.map_fn(-5.0, 5, options));
  EXPECT_EQ(3, options.map_fn(nexttoward(-4.0, -5.0), 5, options));
  EXPECT_EQ(4, options.map_fn(-4.0, 5, options));
}

TEST(HistogramOptionsTest, LinearReverseMapFunctionMapsToLowerBound) {
  // This generates the following histogram:
  //   |      |    |   |    |         |
  // -inf    -10  -8  -6   -4        +inf
  HistogramOptions options =
      HistogramOptions::CustomizedLinear(/*bucket_count*/ 3, /*scalar*/ 2, /*min*/ -10);
  // bucket count differs in 2 due to underflow and overflow additional buckets.
  EXPECT_EQ(-std::numeric_limits<double>::max(),
            options.reverse_map_fn(/*bucket_index*/ 0, 3, options));
  EXPECT_EQ(-10, options.reverse_map_fn(1, 5, options));
  EXPECT_EQ(-8, options.reverse_map_fn(2, 5, options));
  EXPECT_EQ(-6, options.reverse_map_fn(3, 5, options));
  EXPECT_EQ(-4, options.reverse_map_fn(4, 5, options));
}

}  // namespace
}  // namespace cobalt_client
