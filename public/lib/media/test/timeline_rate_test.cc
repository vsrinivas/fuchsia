// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/lib/timeline_rate.h"

#include <limits>

#include "gtest/gtest.h"

namespace media {
namespace {

uint32_t gcd(uint32_t a, uint32_t b) {
  while (b != 0) {
    uint32_t t = a;
    a = b;
    b = t % b;
  }
  return a;
}

// Verifies TimelineRate::Reduce and the constructor, ensuring that the ratio
// subject_delta * common_factor / reference_delta * common_factor is reduced
// to subject_delta / reference_delta. subject_delta and reference_delta need
// to be relatively prime for this to work.
void VerifyReduce(uint32_t subject_delta,
                  uint32_t reference_delta,
                  uint32_t common_factor) {
  // Make sure subject_delta and reference_delta are relatively prime.
  EXPECT_EQ(1u, gcd(subject_delta, reference_delta));

  uint32_t test_subject_delta = subject_delta * common_factor;
  uint32_t test_reference_delta = reference_delta * common_factor;

  // Make sure the constructor reduces.
  TimelineRate rate(test_subject_delta, test_reference_delta);
  EXPECT_EQ(subject_delta, rate.subject_delta());
  EXPECT_EQ(reference_delta, rate.reference_delta());

  // Test the static method.
  TimelineRate::Reduce(&test_subject_delta, &test_reference_delta);
  EXPECT_EQ(subject_delta, test_subject_delta);
  EXPECT_EQ(reference_delta, test_reference_delta);
}

// Verifies the TimelineRate::Scale methods by scaling value by subject_delta
// /
// reference_delta and verifying the result.
void VerifyScale(int64_t value,
                 uint32_t subject_delta,
                 uint32_t reference_delta,
                 int64_t result) {
  // Test the instance method.
  EXPECT_EQ(result, TimelineRate(subject_delta, reference_delta).Scale(value));

  // Test the static method.
  EXPECT_EQ(result, TimelineRate::Scale(value, subject_delta, reference_delta));

  // Test the operators.
  EXPECT_EQ(result, value * TimelineRate(subject_delta, reference_delta));
  EXPECT_EQ(result, TimelineRate(subject_delta, reference_delta) * value);
  if (subject_delta != 0) {
    EXPECT_EQ(result, value / TimelineRate(reference_delta, subject_delta));
  }
}

// Verifies the TimelineRate::Product methods by multiplying the given a and b
// rates and checking the result against the expected rate.
void VerifyProduct(uint32_t a_subject_delta,
                   uint32_t a_reference_delta,
                   uint32_t b_subject_delta,
                   uint32_t b_reference_delta,
                   uint32_t expected_subject_delta,
                   uint32_t expected_reference_delta,
                   bool exact) {
  // Test the first static method.
  uint32_t actual_subject_delta;
  uint32_t actual_reference_delta;
  TimelineRate::Product(a_subject_delta, a_reference_delta, b_subject_delta,
                        b_reference_delta, &actual_subject_delta,
                        &actual_reference_delta, exact);
  EXPECT_EQ(expected_subject_delta, actual_subject_delta);
  EXPECT_EQ(expected_reference_delta, actual_reference_delta);

  // Test the second static method.
  EXPECT_EQ(TimelineRate(expected_subject_delta, expected_reference_delta),
            TimelineRate::Product(
                TimelineRate(a_subject_delta, a_reference_delta),
                TimelineRate(b_subject_delta, b_reference_delta), exact));

  // Test the operator
  if (exact) {
    EXPECT_EQ(TimelineRate(expected_subject_delta, expected_reference_delta),
              TimelineRate(a_subject_delta, a_reference_delta) *
                  TimelineRate(b_subject_delta, b_reference_delta));
  }
}

// Verifies the TimelineRaten::Inverse method using the given rate.
void VerifyInverse(uint32_t subject_delta, uint32_t reference_delta) {
  TimelineRate rate(subject_delta, reference_delta);
  TimelineRate inverse(rate.Inverse());
  EXPECT_EQ(rate.reference_delta(), inverse.subject_delta());
  EXPECT_EQ(rate.subject_delta(), inverse.reference_delta());
}

// Tests TimelineRate::Reduce and that the TimelineRate constructor reduces.
TEST(TimelineRateTest, Reduce) {
  VerifyReduce(0, 1, 1);
  VerifyReduce(1, 1, 1);
  VerifyReduce(1234, 1, 1);
  VerifyReduce(1, 1234, 14);
  VerifyReduce(1, 1, 1234);
  VerifyReduce(10, 1, 1234);
  VerifyReduce(1, 10, 1234);
  VerifyReduce(49, 81, 1);
  VerifyReduce(49, 81, 10);
  VerifyReduce(49, 81, 100);
  VerifyReduce(1, 8, 65536);
  VerifyReduce(8, 1, 65536);
}

// Tests TimelineRate::Scale, static, instance and operator versions.
TEST(TimelineRateTest, Scale) {
  const int64_t int64_min = std::numeric_limits<int64_t>::min();
  VerifyScale(0, 0, 1, 0);
  VerifyScale(1, 0, 1, 0);
  VerifyScale(0, 1, 1, 0);
  VerifyScale(1, 1, 1, 1);
  VerifyScale(1, 2, 1, 2);
  VerifyScale(1, 1, 2, 0);
  VerifyScale(-1, 1, 2, -1);
  VerifyScale(1000, 1, 2, 500);
  VerifyScale(1001, 1, 2, 500);
  VerifyScale(-1000, 1, 2, -500);
  VerifyScale(-1001, 1, 2, -501);
  VerifyScale(1000, 2, 1, 2000);
  VerifyScale(1001, 2, 1, 2002);
  VerifyScale(-1000, 2, 1, -2000);
  VerifyScale(-1001, 2, 1, -2002);
  VerifyScale(1ll << 32, 1, 1, 1ll << 32);
  VerifyScale(1ll << 32, 1, 2, 1ll << 31);
  VerifyScale(1ll << 32, 2, 1, 1ll << 33);
  VerifyScale(1234ll << 30, 1, 1, 1234ll << 30);
  VerifyScale(1234ll << 30, 1, 2, 1234ll << 29);
  VerifyScale(1234ll << 30, 2, 1, 1234ll << 31);
  VerifyScale(1234ll << 30, 1 << 31, 1, TimelineRate::kOverflow);
  VerifyScale(1234ll << 30, 1ll << 31, (1ll << 31) - 2,
              (1234ll << 30) + 1234ll);
  VerifyScale(int64_min, 1, 1, int64_min);
  VerifyScale(int64_min, 1, 2, int64_min / 2);
  VerifyScale(int64_min / 2, 2, 1, int64_min);
  VerifyScale(int64_min, 1000001, 1000000, TimelineRate::kOverflow);
}

// Tests TimelineRate::Product, static and operator versions.
TEST(TimelineRateTest, Product) {
  VerifyProduct(0, 1, 0, 1, 0, 1, true);
  VerifyProduct(1, 1, 1, 1, 1, 1, true);
  VerifyProduct(10, 1, 1, 10, 1, 1, true);
  VerifyProduct(4321, 1234, 617, 4321, 1, 2, true);
  VerifyProduct(1234, 4321, 4321, 617, 2, 1, true);
  VerifyProduct(1ll << 31, (1ll << 31) - 1, (1ll << 31) - 1, 1ll << 31, 1, 1,
                true);
  VerifyProduct(1ll << 31, (1ll << 31) - 1, (1ll << 31) - 2, 1ll << 31,
                0x7ffffffe, 0x7fffffff, false);
}

// Tests TimelineRate::Inverse.
TEST(TimelineRateTest, Inverse) {
  VerifyInverse(1, 1);
  VerifyInverse(2, 1);
  VerifyInverse(1, 2);
  VerifyInverse(1000000, 1234);
  VerifyInverse(1234, 1000000);
}

}  // namespace
}  // namespace media
