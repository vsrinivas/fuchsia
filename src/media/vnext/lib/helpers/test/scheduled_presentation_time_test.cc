// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/vnext/lib/helpers/scheduled_presentation_time.h"

#include <gtest/gtest.h>

namespace fmlib {
namespace {

// Tests the |ToPresentationTime| method.
TEST(ScheduledPresentationTime, ToPresentationTime) {
  constexpr zx::duration kInitialPresentationTime = zx::duration(1234);
  constexpr zx::time kInitialReferenceTime = zx::time(4321);

  constexpr zx::duration kFuturePresentationTime = zx::duration(2345);
  constexpr zx::time kFutureReferenceTime = zx::time(5432);

  ScheduledPresentationTime under_test(kInitialPresentationTime, kInitialReferenceTime);
  EXPECT_EQ(kInitialPresentationTime, under_test.ToPresentationTime(kInitialReferenceTime));
  EXPECT_EQ(kFuturePresentationTime, under_test.ToPresentationTime(kFutureReferenceTime));

  // TODO(dalesat): Add tests for 128-bit math, when that's supported.
}

// Tests the |ToReferenceTime| method.
TEST(ScheduledPresentationTime, ToReferenceTime) {
  constexpr zx::duration kInitialPresentationTime = zx::duration(1234);
  constexpr zx::time kInitialReferenceTime = zx::time(4321);

  constexpr zx::duration kFuturePresentationTime = zx::duration(2345);
  constexpr zx::time kFutureReferenceTime = zx::time(5432);

  ScheduledPresentationTime under_test(kInitialPresentationTime, kInitialReferenceTime);
  EXPECT_EQ(kInitialReferenceTime, under_test.ToReferenceTime(kInitialPresentationTime));
  EXPECT_EQ(kFutureReferenceTime, under_test.ToReferenceTime(kFuturePresentationTime));

  // TODO(dalesat): Add tests for 128-bit math, when that's supported.
}

}  // namespace
}  // namespace fmlib
