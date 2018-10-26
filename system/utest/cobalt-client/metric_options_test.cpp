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

bool TestLocal() {
    BEGIN_TEST;
    MetricOptions options;
    options.Remote();
    options.Local();
    ASSERT_TRUE(options.IsLocal());
    ASSERT_FALSE(options.IsRemote());
    END_TEST;
}

bool TestRemote() {
    BEGIN_TEST;
    MetricOptions options;
    options.Local();
    options.Remote();
    ASSERT_TRUE(options.IsRemote());
    ASSERT_FALSE(options.IsLocal());
    END_TEST;
}

bool TestBoth() {
    BEGIN_TEST;
    MetricOptions options;
    options.Local();
    options.Both();
    ASSERT_TRUE(options.IsRemote());
    ASSERT_TRUE(options.IsLocal());
    END_TEST;
}

bool TestMakeExponentialOptions() {
    BEGIN_TEST;
    HistogramOptions options =
        HistogramOptions::Exponential(/*bucket_count=*/3, /*base=*/4, /*scalar=*/2, /*offset=*/-10);
    ASSERT_EQ(options.bucket_count, 3);
    ASSERT_EQ(options.base, 4);
    ASSERT_EQ(options.scalar, 2);
    // the calculated offset is such that it guarantees that it matches the lower bound of the first
    // bucket(excluding underflow bucket). This is to take in account:
    // offset = scalar * base ^(0) + calculated_offset
    // calculated_offset = offset - scalar.
    ASSERT_EQ(options.offset, -12);
    ASSERT_EQ(options.type, HistogramOptions::Type::kExponential);
    ASSERT_TRUE(options.IsValid());
    END_TEST;
}

bool TestExponentialInvalidBucketCount() {
    BEGIN_TEST;
    HistogramOptions options =
        HistogramOptions::Exponential(/*bucket_count=*/0, /*base=*/1, /*scalar=*/2, /*offset=*/-10);
    ASSERT_FALSE(options.IsValid());
    END_TEST;
}

bool TestExponentialInvalidBase() {
    BEGIN_TEST;
    HistogramOptions options =
        HistogramOptions::Exponential(/*bucket_count=*/1, /*base=*/0, /*scalar=*/2, /*offset=*/-10);
    ASSERT_FALSE(options.IsValid());
    END_TEST;
}

bool TestExponentialInvalidScalar() {
    BEGIN_TEST;
    HistogramOptions options =
        HistogramOptions::Exponential(/*bucket_count=*/1, /*base=*/1, /*scalar=*/0, /*offset=*/-10);
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
    HistogramOptions options =
        HistogramOptions::Exponential(/*bucket_count=*/3, /*base=*/2, /*scalar=*/3, /*offset=*/5);
    EXPECT_EQ(options.map_fn(/*value=*/4, options), 0);
    EXPECT_EQ(options.map_fn(nextafter(5, 4), options), 0);
    EXPECT_EQ(options.map_fn(5, options), 1);
    EXPECT_EQ(options.map_fn(7.5, options), 1);
    EXPECT_EQ(options.map_fn(nextafter(8, 7), options), 1);
    EXPECT_EQ(options.map_fn(8, options), 2);
    EXPECT_EQ(options.map_fn(12, options), 2);
    EXPECT_EQ(options.map_fn(nextafter(12, 11), options), 2);
    EXPECT_EQ(options.map_fn(14, options), 3);
    EXPECT_EQ(options.map_fn(18, options), 3);
    EXPECT_EQ(options.map_fn(nextafter(26, 25), options), 3);
    EXPECT_EQ(options.map_fn(26, options), 4);
    END_TEST;
}

bool TestExponentialReverseMap() {
    BEGIN_TEST;
    // This generates the following histogram:
    //   |      | |  |        |         |
    // -inf     5 8  14       26      +inf
    HistogramOptions options =
        HistogramOptions::Exponential(/*bucket_count=*/3, /*base=*/2, /*scalar=*/3, /*offset=*/5);
    EXPECT_EQ(options.reverse_map_fn(/*bucket_index=*/0, options), -DBL_MAX);
    EXPECT_EQ(options.reverse_map_fn(1, options), 5);
    EXPECT_EQ(options.reverse_map_fn(2, options), 8);
    EXPECT_EQ(options.reverse_map_fn(3, options), 14);
    EXPECT_EQ(options.reverse_map_fn(4, options), 26);
    END_TEST;
}

bool TestMakeLinearOptions() {
    BEGIN_TEST;
    HistogramOptions options =
        HistogramOptions::Linear(/*bucket_count=*/3, /*scalar=*/2, /*offset=*/-10);
    ASSERT_EQ(options.bucket_count, 3);
    ASSERT_EQ(options.base, 1);
    ASSERT_EQ(options.scalar, 2);
    ASSERT_EQ(options.offset, -10);
    ASSERT_EQ(options.type, HistogramOptions::Type::kLinear);
    ASSERT_TRUE(options.map_fn);
    ASSERT_TRUE(options.reverse_map_fn);
    ASSERT_TRUE(options.IsValid());
    END_TEST;
}

bool TestLinearInvalidBucketCount() {
    BEGIN_TEST;
    HistogramOptions options =
        HistogramOptions::Linear(/*bucket_count=*/0, /*scalar=*/2, /*offset=*/-10);
    ASSERT_FALSE(options.IsValid());
    END_TEST;
}

bool TestLinearInvalidScalar() {
    BEGIN_TEST;
    HistogramOptions options =
        HistogramOptions::Linear(/*bucket_count=*/1, /*scalar=*/0, /*offset=*/-10);
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
        HistogramOptions::Linear(/*bucket_count=*/3, /*scalar=*/2, /*offset=*/-10);
    EXPECT_EQ(options.map_fn(/*value=*/-15, options), 0);
    EXPECT_EQ(options.map_fn(nextafter(-10.0, -11), options), 0);
    EXPECT_EQ(options.map_fn(-10.0, options), 1);
    EXPECT_EQ(options.map_fn(-9.0, options), 1);
    EXPECT_EQ(options.map_fn(-8.0, options), 2);
    EXPECT_EQ(options.map_fn(-7.0, options), 2);
    EXPECT_EQ(options.map_fn(-6.0, options), 3);
    EXPECT_EQ(options.map_fn(-5.0, options), 3);
    EXPECT_EQ(options.map_fn(nexttoward(-4.0, -5.0), options), 3);
    EXPECT_EQ(options.map_fn(-4.0, options), 4);
    END_TEST;
}

bool TestLinearReverseMap() {
    BEGIN_TEST;
    // This generates the following histogram:
    //   |      |    |   |    |         |
    // -inf    -10  -8  -6   -4        +inf
    HistogramOptions options =
        HistogramOptions::Linear(/*bucket_count=*/3, /*scalar=*/2, /*offset=*/-10);
    EXPECT_EQ(options.reverse_map_fn(/*bucket_index=*/0, options), -DBL_MAX);
    EXPECT_EQ(options.reverse_map_fn(1, options), -10);
    EXPECT_EQ(options.reverse_map_fn(2, options), -8);
    EXPECT_EQ(options.reverse_map_fn(3, options), -6);
    EXPECT_EQ(options.reverse_map_fn(4, options), -4);
    END_TEST;
}

BEGIN_TEST_CASE(MetricOptionsTest)
RUN_TEST(TestLocal)
RUN_TEST(TestRemote)
RUN_TEST(TestBoth)
END_TEST_CASE(MetricOptionsTest);

BEGIN_TEST_CASE(HistogramOptionsTest)
RUN_TEST(TestMakeExponentialOptions)
RUN_TEST(TestExponentialInvalidBucketCount)
RUN_TEST(TestExponentialInvalidBase)
RUN_TEST(TestExponentialInvalidScalar)
RUN_TEST(TestExponentialMap)
RUN_TEST(TestExponentialReverseMap)
RUN_TEST(TestMakeLinearOptions)
RUN_TEST(TestLinearInvalidBucketCount)
RUN_TEST(TestLinearInvalidScalar)
RUN_TEST(TestLinearMap)
RUN_TEST(TestLinearReverseMap)
END_TEST_CASE(HistogramOptionsTest)
} // namespace
} // namespace cobalt_client
