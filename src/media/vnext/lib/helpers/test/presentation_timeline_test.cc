// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/vnext/lib/helpers/presentation_timeline.h"

#include <gtest/gtest.h>

namespace fmlib {
namespace {

// Tests the |fidl| methods.
TEST(PresentationTimeline, Fidl) {
  {
    PresentationTimeline under_test;
    auto result = under_test.fidl();
    EXPECT_EQ(0, result.initial_presentation_time);
    EXPECT_EQ(0, result.initial_reference_time);
    EXPECT_EQ(1.0f, result.rate);
    EXPECT_EQ(false, result.progressing);
  }

  {
    constexpr zx::duration kInitialPresentationTime = zx::duration(1234);
    constexpr zx::time kInitialReferenceTime = zx::time(4321);
    constexpr float kRate = 1.0f;
    constexpr bool kProgressing = true;

    PresentationTimeline under_test(kInitialPresentationTime, kInitialReferenceTime, kRate,
                                    kProgressing);
    auto result = under_test.fidl();
    EXPECT_EQ(kInitialPresentationTime.get(), result.initial_presentation_time);
    EXPECT_EQ(kInitialReferenceTime.get(), result.initial_reference_time);
    EXPECT_EQ(kRate, result.rate);
    EXPECT_EQ(kProgressing, result.progressing);
  }
}

// Tests the |ToPresentationTime| method.
TEST(PresentationTimeline, ToPresentationTime) {
  constexpr zx::duration kInitialPresentationTime = zx::duration(1234);
  constexpr zx::time kInitialReferenceTime = zx::time(4321);
  constexpr float kRate = 1.0f;
  constexpr bool kProgressing = true;

  constexpr zx::duration kFuturePresentationTime = zx::duration(2345);
  constexpr zx::time kFutureReferenceTime = zx::time(5432);

  PresentationTimeline under_test(kInitialPresentationTime, kInitialReferenceTime, kRate,
                                  kProgressing);
  EXPECT_EQ(kInitialPresentationTime, under_test.ToPresentationTime(kInitialReferenceTime));
  EXPECT_EQ(kFuturePresentationTime, under_test.ToPresentationTime(kFutureReferenceTime));

  under_test.progressing() = false;
  EXPECT_EQ(kInitialPresentationTime, under_test.ToPresentationTime(zx::time(0)));
  EXPECT_EQ(kInitialPresentationTime, under_test.ToPresentationTime(kInitialReferenceTime));
  EXPECT_EQ(kInitialPresentationTime, under_test.ToPresentationTime(kFutureReferenceTime));

  // TODO(dalesat): Add tests for 128-bit math, when that's supported.
  // TODO(dalesat): Add tests for rates other than 1, when that's supported.
}

// Tests the |ToReferenceTime| method.
TEST(PresentationTimeline, ToReferenceTime) {
  constexpr zx::duration kInitialPresentationTime = zx::duration(1234);
  constexpr zx::time kInitialReferenceTime = zx::time(4321);
  constexpr float kRate = 1.0f;
  constexpr bool kProgressing = true;

  constexpr zx::duration kFuturePresentationTime = zx::duration(2345);
  constexpr zx::time kFutureReferenceTime = zx::time(5432);

  PresentationTimeline under_test(kInitialPresentationTime, kInitialReferenceTime, kRate,
                                  kProgressing);
  EXPECT_EQ(kInitialReferenceTime, under_test.ToReferenceTime(kInitialPresentationTime));
  EXPECT_EQ(kFutureReferenceTime, under_test.ToReferenceTime(kFuturePresentationTime));

  under_test.progressing() = false;
  EXPECT_EQ(kInitialReferenceTime, under_test.ToReferenceTime(kInitialPresentationTime));
  EXPECT_EQ(kFutureReferenceTime, under_test.ToReferenceTime(kFuturePresentationTime));

  // TODO(dalesat): Add tests for 128-bit math, when that's supported.
  // TODO(dalesat): Add tests for rates other than 1, when that's supported.
}

}  // namespace
}  // namespace fmlib
