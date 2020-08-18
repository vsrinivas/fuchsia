// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "timeline_rate.h"

#include <limits>

#include <gtest/gtest.h>

namespace media {
namespace {

uint64_t gcd(uint64_t a, uint64_t b) {
  while (b != 0) {
    uint64_t t = a;
    a = b;
    b = t % b;
  }
  return a;
}

// Verifies TimelineRate::Reduce and the TimelineRate constructor, ensuring that the ratio
// (subject_delta * common_factor) / (reference_delta * common_factor) is reduced to (subject_delta
// / reference_delta). This requires that subject_delta and reference_delta be relatively prime.
void VerifyReduce(uint64_t subject_delta, uint64_t reference_delta, uint64_t common_factor) {
  // Ensure subject_delta and reference_delta are relatively prime.
  ASSERT_EQ(1u, gcd(subject_delta, reference_delta));

  uint64_t test_subject_delta = subject_delta * common_factor;
  uint64_t test_reference_delta = reference_delta * common_factor;

  // Make sure the constructor reduces.
  TimelineRate rate(test_subject_delta, test_reference_delta);
  EXPECT_EQ(subject_delta, rate.subject_delta());
  EXPECT_EQ(reference_delta, rate.reference_delta());

  // Test the static Reduce method.
  TimelineRate::Reduce(&test_subject_delta, &test_reference_delta);
  EXPECT_EQ(subject_delta, test_subject_delta);
  EXPECT_EQ(reference_delta, test_reference_delta);
}

// Verifies TimelineRate::Scale of a given value, by a (subject_delta / reference_delta) rate.
void VerifyScale(int64_t value, uint64_t subject_delta, uint64_t reference_delta, int64_t result) {
  // Test the instance method.
  EXPECT_EQ(result, TimelineRate(subject_delta, reference_delta).Scale(value));
  if (TimelineRate(subject_delta, reference_delta).invertible()) {
    EXPECT_EQ(result, TimelineRate(reference_delta, subject_delta).ScaleInverse(value));
  }

  // Test the static method.
  EXPECT_EQ(result, TimelineRate::Scale(value, subject_delta, reference_delta));

  // Test the three operators.
  EXPECT_EQ(result, value * TimelineRate(subject_delta, reference_delta));
  EXPECT_EQ(result, TimelineRate(subject_delta, reference_delta) * value);
  if (subject_delta != 0) {
    EXPECT_EQ(result, value / TimelineRate(reference_delta, subject_delta));
  }
}

// Verifies TimelineRate::Product of given a and b timeline rates.
void VerifyProduct(uint64_t a_subject_delta, uint64_t a_reference_delta, uint64_t b_subject_delta,
                   uint64_t b_reference_delta, uint64_t expected_subject_delta,
                   uint64_t expected_reference_delta, bool exact) {
  // Test the static TimelineRate::Product that uses out parameters.
  uint64_t actual_subject_delta;
  uint64_t actual_reference_delta;
  TimelineRate::Product(a_subject_delta, a_reference_delta, b_subject_delta, b_reference_delta,
                        &actual_subject_delta, &actual_reference_delta, exact);
  EXPECT_EQ(expected_subject_delta, actual_subject_delta);
  EXPECT_EQ(expected_reference_delta, actual_reference_delta);

  // Test the static TimelineRate::Product that returns a TimelineRate.
  EXPECT_EQ(TimelineRate(expected_subject_delta, expected_reference_delta),
            TimelineRate::Product(TimelineRate(a_subject_delta, a_reference_delta),
                                  TimelineRate(b_subject_delta, b_reference_delta), exact));

  // Test the * operator
  if (exact) {
    EXPECT_EQ(TimelineRate(expected_subject_delta, expected_reference_delta),
              TimelineRate(a_subject_delta, a_reference_delta) *
                  TimelineRate(b_subject_delta, b_reference_delta));
  }
}

// Verifies TimelineRate::Inverse with the given rate.
void VerifyInverse(uint64_t subject_delta, uint64_t reference_delta) {
  TimelineRate rate(subject_delta, reference_delta);
  TimelineRate inverse(rate.Inverse());
  EXPECT_EQ(rate.reference_delta(), inverse.subject_delta());
  EXPECT_EQ(rate.subject_delta(), inverse.reference_delta());
}

// Verifies TimelineRate::ScaleInverse (this relies on ctor, Scale and Inverse working properly)
void VerifyScaleInverse(int64_t value, uint64_t subject_delta, uint64_t reference_delta) {
  TimelineRate rate(subject_delta, reference_delta);
  TimelineRate inverse(reference_delta, subject_delta);

  EXPECT_EQ(rate.Scale(value), inverse.ScaleInverse(value));
  EXPECT_EQ(rate.ScaleInverse(value), inverse.Scale(value));
}

// Tests ctor(float). Although converted internally to double, incoming floats have limitations.
TEST(TimelineRateTest, Constructor_Float) {
  TimelineRate unity_float(1.0f);
  EXPECT_EQ(unity_float.subject_delta(), 1ull);
  EXPECT_EQ(unity_float.reference_delta(), 1ull);

  TimelineRate basic_float(0.515625f);
  EXPECT_EQ(basic_float.subject_delta(), 33ull);
  EXPECT_EQ(basic_float.reference_delta(), 64ull);

  // 8388608 is 2^23, and this float value is 1/2^23.
  TimelineRate epsilon_float(0.00000011920928955078125f);
  EXPECT_EQ(epsilon_float.subject_delta(), 1ull);
  EXPECT_EQ(epsilon_float.reference_delta(), 8'388'608ull);

  // float's 23-bit mantissa cannot perfectly capture this value; without the last bit this is 3/4.
  TimelineRate float_precision_inadequate(0.75000002f);
  EXPECT_EQ(float_precision_inadequate.subject_delta(), 3ull);
  EXPECT_EQ(float_precision_inadequate.reference_delta(), 4ull);
}

// Tests the double-based TimelineRate constructor
TEST(TimelineRateTest, Constructor_Double) {
  TimelineRate unity_double(1.0);
  EXPECT_EQ(unity_double.subject_delta(), 1ull);
  EXPECT_EQ(unity_double.reference_delta(), 1ull);

  TimelineRate basic_double(0.09375);
  EXPECT_EQ(basic_double.subject_delta(), 3ull);
  EXPECT_EQ(basic_double.reference_delta(), 32ull);

  // 8388608 is 2^23, and this float value is 1/2^23.
  TimelineRate float_epsilon_double(0.00000011920928955078125);
  EXPECT_EQ(float_epsilon_double.subject_delta(), 1ull);
  EXPECT_EQ(float_epsilon_double.reference_delta(), 8'388'608ull);

  // double's 52-bit mantissa can accommodate this precision. This should be larger than 3/4; we
  // compare without division so we don't lose precision related to any modulo.
  TimelineRate double_precision_adequate(0.75000002);
  EXPECT_GT(double_precision_adequate.subject_delta(), 3ull);
  EXPECT_GT(double_precision_adequate.reference_delta(), 4ull);
  EXPECT_GT(static_cast<__uint128_t>(double_precision_adequate.subject_delta()) * 4,
            static_cast<__uint128_t>(double_precision_adequate.reference_delta()) * 3);

  // 4'503'599'627'370'496 is 2^52, and this double value is evaluated as 1/2^52.
  TimelineRate epsilon_double(2.221e-16);
  EXPECT_EQ(epsilon_double.subject_delta(), 1ull);
  EXPECT_EQ(epsilon_double.reference_delta(), 4'503'599'627'370'496ull);

  // Because this value is just less than 1/2^52, our conversion will treat this as zero.
  TimelineRate below_epsilon_double(2.220e-16);
  EXPECT_EQ(below_epsilon_double.subject_delta(), 0ull);
  EXPECT_EQ(below_epsilon_double.reference_delta(), 1ull);
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

  VerifyScale(1234ll << 30, 1 << 22, 1, 1234ll << 52);
  VerifyScale(1ll << 30, 1234ll << 32, 1 << 10, 1234ll << 52);
  VerifyScale(1234ll << 30, 1ll << 31, (1ll << 31) - 2, (1234ll << 30) + 1234ll);

  // int64_max is odd so we include -1 or -3 to eliminate modulo. Fractional leftover aside, the
  // absence of overflow or wraparound indicates a successful muldiv using 128 bits internally.
  const int64_t int64_max = std::numeric_limits<int64_t>::max();
  VerifyScale(int64_max, 1, 1, int64_max);
  VerifyScale(int64_max - 1, 1, 2, (int64_max - 1) / 2);
  VerifyScale(int64_max - 3, 3, 4, ((int64_max - 3) / 4) * 3);
  VerifyScale((int64_max - 1) / 2, 2, 1, int64_max - 1);
  VerifyScale(int64_max, 1000001, 1000000, TimelineRate::kOverflow);

  const int64_t int64_min = std::numeric_limits<int64_t>::min();
  VerifyScale(int64_min, 1, 1, int64_min);
  VerifyScale(int64_min, 1, 2, int64_min / 2);
  VerifyScale(int64_min, 3, 4, (int64_min / 4) * 3);
  VerifyScale(int64_min / 2, 2, 1, int64_min);
  VerifyScale(int64_min, 1000001, 1000000, TimelineRate::kOverflow);

  VerifyScale(85'681'756'014'041, 95'999'904, 244'140'625, 33'691'403'681'379);
}

// Tests TimelineRate::Product, static and operator versions.
TEST(TimelineRateTest, Product) {
  VerifyProduct(0, 1, 0, 1, 0, 1, true);
  VerifyProduct(1, 1, 1, 1, 1, 1, true);
  VerifyProduct(10, 1, 1, 10, 1, 1, true);
  VerifyProduct(4321, 1234, 617, 4321, 1, 2, true);
  VerifyProduct(1234, 4321, 4321, 617, 2, 1, true);
  VerifyProduct(1ll << 31, (1ll << 31) - 1, (1ll << 31) - 1, 1ll << 31, 1, 1, true);
  VerifyProduct(1ll << 31, (1ll << 31) - 1, (1ll << 31) - 2, 1ll << 31, 0x7ffffffe, 0x7fffffff,
                false);
}

// Tests TimelineRate::Inverse.
TEST(TimelineRateTest, Inverse) {
  VerifyInverse(1, 1);
  VerifyInverse(2, 1);
  VerifyInverse(1, 2);
  VerifyInverse(1000000, 1234);
  VerifyInverse(1234, 1000000);
}

// Tests TimelineRate::ScaleInverse.
TEST(TimelineRateTest, ScaleInverse) {
  VerifyScaleInverse(1, 1, 1);
  VerifyScaleInverse(4, 2, 1);
  VerifyScaleInverse(42, 1, 2);
  VerifyScaleInverse(1234000, 1000, 1234);
  VerifyScaleInverse(24680000, 1234, 1000);
}

}  // namespace
}  // namespace media
