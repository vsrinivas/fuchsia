// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/clock/clone_mono.h"

#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/media/audio/lib/clock/testing/clock_test.h"

namespace media::audio::clock {

TEST(CloneMonoTest, AdjustableCloneIsSameAsClockMonotonic) {
  auto adjustable_clock = AdjustableCloneOfMonotonic();
  EXPECT_TRUE(adjustable_clock.is_valid());

  testing::VerifyAdvances(adjustable_clock);
  testing::VerifyIsSystemMonotonic(adjustable_clock);
}

TEST(CloneMonoTest, ReadableCloneIsSameAsClockMonotonic) {
  auto readable_clock = CloneOfMonotonic();
  EXPECT_TRUE(readable_clock.is_valid());

  testing::VerifyAdvances(readable_clock);
  testing::VerifyIsSystemMonotonic(readable_clock);
}

TEST(CloneMonoTest, AdjustableClockCanBeAdjusted) {
  auto adjustable_clock = AdjustableCloneOfMonotonic();
  EXPECT_TRUE(adjustable_clock.is_valid());

  testing::VerifyCanBeRateAdjusted(adjustable_clock);
}

TEST(CloneMonoTest, ReadonlyClockCannotBeAdjusted) {
  auto readable_clock = CloneOfMonotonic();
  EXPECT_TRUE(readable_clock.is_valid());

  testing::VerifyReadOnlyRights(readable_clock);
  testing::VerifyCannotBeRateAdjusted(readable_clock);
}

}  // namespace media::audio::clock
