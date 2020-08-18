// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "timeline_function.h"

#include <gtest/gtest.h>

namespace media {
namespace {

// Verifies that a TimelineFunction instantiated with provided arguments has expected properties.
void VerifyBasics(const TimelineFunction& under_test, int64_t subject_time, int64_t reference_time,
                  uint64_t subject_delta, uint64_t reference_delta) {
  TimelineRate::Reduce(&subject_delta, &reference_delta);
  EXPECT_EQ(reference_time, under_test.reference_time());
  EXPECT_EQ(subject_time, under_test.subject_time());
  EXPECT_EQ(reference_delta, under_test.reference_delta());
  EXPECT_EQ(subject_delta, under_test.subject_delta());
  EXPECT_EQ(reference_delta, under_test.rate().reference_delta());
  EXPECT_EQ(subject_delta, under_test.rate().subject_delta());
}

// Verifies that TimelineFunctions with given arguments (instantiated 3 different ways) have
// expected properties.
void VerifyBasics(int64_t subject_time, int64_t reference_time, uint64_t subject_delta,
                  uint64_t reference_delta) {
  TimelineFunction under_test_1(subject_time, reference_time, subject_delta, reference_delta);
  VerifyBasics(under_test_1, subject_time, reference_time, subject_delta, reference_delta);

  TimelineFunction under_test_2(subject_time, reference_time,
                                TimelineRate(subject_delta, reference_delta));
  VerifyBasics(under_test_2, subject_time, reference_time, subject_delta, reference_delta);

  TimelineFunction under_test_3(TimelineRate(subject_delta, reference_delta));
  VerifyBasics(under_test_3, 0, 0, subject_delta, reference_delta);

  EXPECT_EQ(under_test_1, under_test_1);
  EXPECT_EQ(under_test_1, under_test_2);
  EXPECT_EQ(under_test_2, under_test_1);
  EXPECT_EQ(under_test_2, under_test_2);

  if (reference_time == 0 && subject_time == 0) {
    EXPECT_EQ(under_test_1, under_test_3);
    EXPECT_EQ(under_test_2, under_test_3);
    EXPECT_EQ(under_test_3, under_test_1);
    EXPECT_EQ(under_test_3, under_test_2);
  } else {
    EXPECT_NE(under_test_1, under_test_3);
    EXPECT_NE(under_test_2, under_test_3);
    EXPECT_NE(under_test_3, under_test_1);
    EXPECT_NE(under_test_3, under_test_2);
  }
}

// Verifies that inverses of TimelineFunctions with given arguments (instantiated 3 different ways)
// have expected properties.
void VerifyInverse(int64_t subject_time, int64_t reference_time, uint64_t subject_delta,
                   uint64_t reference_delta) {
  TimelineFunction under_test_1(subject_time, reference_time, subject_delta, reference_delta);
  VerifyBasics(under_test_1.Inverse(), reference_time, subject_time, reference_delta,
               subject_delta);

  TimelineFunction under_test_2(subject_time, reference_time,
                                TimelineRate(subject_delta, reference_delta));
  VerifyBasics(under_test_2.Inverse(), reference_time, subject_time, reference_delta,
               subject_delta);

  TimelineFunction under_test_3(TimelineRate(subject_delta, reference_delta));
  VerifyBasics(under_test_3.Inverse(), 0, 0, reference_delta, subject_delta);
}

// Verifies that TimelineFunction::Apply (in various forms) works as expected for given arguments.
void VerifyApply(int64_t subject_time, int64_t reference_time, uint64_t subject_delta,
                 uint64_t reference_delta, int64_t reference_input, int64_t expected_result) {
  // Verify the static method.
  EXPECT_EQ(expected_result,
            TimelineFunction::Apply(subject_time, reference_time,
                                    TimelineRate(subject_delta, reference_delta), reference_input));

  // Verify the instance method.
  TimelineFunction under_test(subject_time, reference_time, subject_delta, reference_delta);
  EXPECT_EQ(expected_result, under_test.Apply(reference_input));

  // Verify the operator.
  EXPECT_EQ(expected_result, under_test(reference_input));
}

// Verifies TimelineFunction::ApplyInverse (in various forms) works, for given arguments.
void VerifyApplyInverse(int64_t subject_time, int64_t reference_time, uint64_t subject_delta,
                        uint64_t reference_delta, int64_t subject_input, int64_t expected_result) {
  // Verify the static method.
  EXPECT_EQ(expected_result, TimelineFunction::ApplyInverse(
                                 subject_time, reference_time,
                                 TimelineRate(subject_delta, reference_delta), subject_input));

  // Verify the instance method.
  TimelineFunction under_test(subject_time, reference_time, subject_delta, reference_delta);
  EXPECT_EQ(expected_result, under_test.ApplyInverse(subject_input));
}

// Verifies that TimelineFunction::Compose works as expected for given inputs.
void VerifyCompose(const TimelineFunction& a, const TimelineFunction& b, bool exact,
                   const TimelineFunction& expected_result) {
  // Verify the static method.
  EXPECT_EQ(expected_result, TimelineFunction::Compose(a, b, exact));
}

// Tests TimelineFunction basics for various instantiation arguments.
TEST(TimelineFunctionTest, Basics) {
  VerifyBasics(0, 0, 0, 1);
  VerifyBasics(0, 0, 1, 1);
  VerifyBasics(1, 1, 10, 10);
  VerifyBasics(5678, 1234, 8765, 4321);
  VerifyBasics(5678, -1234, 8765, 4321);
  VerifyBasics(-5678, -1234, 8765, 4321);
  VerifyBasics(-5678, 1234, 8765, 4321);
}

// Tests TimelineFunction::Inverse.
TEST(TimelineFunctionTest, Inverse) {
  VerifyInverse(0, 0, 1, 1);
  VerifyInverse(1, 1, 10, 10);
  VerifyInverse(5678, 1234, 8765, 4321);
  VerifyInverse(5678, -1234, 8765, 4321);
  VerifyInverse(-5678, -1234, 8765, 4321);
  VerifyInverse(-5678, 1234, 8765, 4321);
}

// Tests TimelineFunction::Apply in its variations.
TEST(TimelineFunctionTest, Apply) {
  VerifyApply(0, 0, 0, 1, 0, 0);
  VerifyApply(0, 0, 0, 1, 1000, 0);
  VerifyApply(1234, 0, 0, 1, 0, 1234);
  VerifyApply(1234, 0, 0, 1, 1000, 1234);
  VerifyApply(1234, 0, 0, 1, -1000, 1234);
  VerifyApply(-1234, 0, 0, 1, 0, -1234);
  VerifyApply(-1234, 0, 0, 1, 1000, -1234);
  VerifyApply(-1234, 0, 0, 1, -1000, -1234);
  VerifyApply(0, 0, 1, 1, 0, 0);
  VerifyApply(0, 0, 1, 1, 1000, 1000);
  VerifyApply(1234, 0, 1, 1, 0, 1234);
  VerifyApply(1234, 0, 1, 1, 1000, 2234);
  VerifyApply(1234, 0, 1, 1, -1000, 234);
  VerifyApply(-1234, 0, 1, 1, 0, -1234);
  VerifyApply(-1234, 0, 1, 1, 1000, -234);
  VerifyApply(-1234, 0, 1, 1, -1000, -2234);
  VerifyApply(0, 10, 0, 1, 0, 0);
  VerifyApply(0, 10, 1, 1, 0, -10);
  VerifyApply(0, -10, 0, 1, 0, 0);
  VerifyApply(0, -10, 1, 1, 0, 10);
  VerifyApply(1234, 0, 1, 2, 0, 1234);
  VerifyApply(1234, 0, 1, 2, 1234, 1234 + 1234 / 2);
  VerifyApply(1234, 0, 2, 1, 1234, 1234 + 1234 * 2);
}

// Tests TimelineFunction::Apply in its variations.
TEST(TimelineFunctionTest, ApplyInverse) {
  VerifyApplyInverse(0, 0, 1, 1, 0, 0);
  VerifyApplyInverse(0, 0, 1, 1, 1000, 1000);
  VerifyApplyInverse(1234, 0, 1, 1, 1234, 0);
  VerifyApplyInverse(1234, 0, 1, 1, 2234, 1000);
  VerifyApplyInverse(1234, 0, 1, 1, 234, -1000);
  VerifyApplyInverse(-1234, 0, 1, 1, -1234, 0);
  VerifyApplyInverse(-1234, 0, 1, 1, -234, 1000);
  VerifyApplyInverse(-1234, 0, 1, 1, -2234, -1000);
  VerifyApplyInverse(0, 10, 1, 1, -10, 0);
  VerifyApplyInverse(0, -10, 1, 1, 10, 0);
  VerifyApplyInverse(1234, 0, 1, 2, 1234, 0);
  VerifyApplyInverse(1234, 0, 1, 2, 1234 + 1234 / 2, 1234);
  VerifyApplyInverse(1234, 0, 2, 1, 1234 + 1234 * 2, 1234);
}

// Tests TimelineFunction::Compose.
TEST(TimelineFunctionTest, Compose) {
  VerifyCompose(TimelineFunction(0, 0, 0, 1), TimelineFunction(0, 0, 0, 1), true,
                TimelineFunction(0, 0, 0, 1));
  VerifyCompose(TimelineFunction(0, 0, 1, 1), TimelineFunction(0, 0, 1, 1), true,
                TimelineFunction(0, 0, 1, 1));
  VerifyCompose(TimelineFunction(0, 1, 1, 1), TimelineFunction(0, 0, 1, 1), true,
                TimelineFunction(-1, 0, 1, 1));
  VerifyCompose(TimelineFunction(10, 10, 1, 1), TimelineFunction(0, 0, 1, 1), true,
                TimelineFunction(0, 0, 1, 1));
  VerifyCompose(TimelineFunction(0, 0, 2, 1), TimelineFunction(0, 0, 2, 1), true,
                TimelineFunction(0, 0, 4, 1));
  VerifyCompose(TimelineFunction(0, 0, 1, 2), TimelineFunction(0, 0, 1, 2), true,
                TimelineFunction(0, 0, 1, 4));
  VerifyCompose(TimelineFunction(0, 0, 1, 2), TimelineFunction(0, 0, 2, 1), true,
                TimelineFunction(0, 0, 1, 1));
}

}  // namespace
}  // namespace media
