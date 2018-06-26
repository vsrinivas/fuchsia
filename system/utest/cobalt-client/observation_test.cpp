// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cobalt-client/cpp/observation.h>
#include <fuchsia/cobalt/c/fidl.h>
#include <unittest/unittest.h>

namespace cobalt_client {
namespace {

bool IntValueTest() {
    BEGIN_TEST;
    Value int_val = IntValue(32);
    ASSERT_EQ(int_val.tag, fuchsia_cobalt_ValueTagint_value);
    ASSERT_EQ(int_val.int_value, 32);
    END_TEST;
}

bool DoubleValueTest() {
    BEGIN_TEST;
    Value dbl_val = DoubleValue(1e-8);
    ASSERT_EQ(dbl_val.tag, fuchsia_cobalt_ValueTagdouble_value);
    ASSERT_EQ(dbl_val.double_value, 1e-8);
    END_TEST;
}

bool IndexValueTest() {
    BEGIN_TEST;
    Value index_val = IndexValue(32);
    ASSERT_EQ(index_val.tag, fuchsia_cobalt_ValueTagindex_value);
    ASSERT_EQ(index_val.index_value, 32);
    END_TEST;
}

bool BucketDistributionValueTest() {
    BEGIN_TEST;
    DistributionEntry entries[5];
    Value buckets_val = BucketDistributionValue(5, entries);
    ASSERT_EQ(buckets_val.tag, fuchsia_cobalt_ValueTagint_bucket_distribution);
    ASSERT_EQ(buckets_val.int_bucket_distribution.count, 5);
    ASSERT_EQ(buckets_val.int_bucket_distribution.data, entries);
    END_TEST;
}

BEGIN_TEST_CASE(ObservationTest)
RUN_TEST(IntValueTest)
RUN_TEST(DoubleValueTest)
RUN_TEST(IndexValueTest)
RUN_TEST(BucketDistributionValueTest)
END_TEST_CASE(ObservationTest)

} // namespace
} // namespace cobalt_client
