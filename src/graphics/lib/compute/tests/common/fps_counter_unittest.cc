// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tests/common/fps_counter.h"

#include <gtest/gtest.h>

// Helper class that implements a clock for fps_counter.h during unit-testing.
struct ScopedTestClock
{
  ScopedTestClock()
  {
    fps_counter_set_clock_for_testing(&callback, this);
  }

  ~ScopedTestClock()
  {
    fps_counter_set_clock_for_testing(nullptr, nullptr);
  }

  double seconds = 0.;

  static double
  callback(void * opaque)
  {
    return reinterpret_cast<ScopedTestClock *>(opaque)->seconds;
  }
};

// Should match the implementation.
static const double kFpsCounterPeriodSeconds = 4.0;

TEST(FpsCounterTest, StartStopTest)
{
  ScopedTestClock clock;
  fps_counter_t   counter;

  fps_counter_start(&counter);
  EXPECT_FALSE(fps_counter_stop(&counter));
}

TEST(FpsCounterTest, SingleTickTest)
{
  ScopedTestClock clock;
  fps_counter_t   counter;

  fps_counter_start(&counter);
  clock.seconds += 1.0;
  EXPECT_FALSE(fps_counter_tick(&counter));
  EXPECT_FALSE(fps_counter_stop(&counter));

  fps_counter_start(&counter);
  clock.seconds += kFpsCounterPeriodSeconds;
  EXPECT_TRUE(fps_counter_tick(&counter));
  EXPECT_FLOAT_EQ(counter.current_fps, 1 / kFpsCounterPeriodSeconds);
  EXPECT_FALSE(fps_counter_stop(&counter));

  fps_counter_start(&counter);
  clock.seconds += kFpsCounterPeriodSeconds / 2;
  EXPECT_FALSE(fps_counter_tick(&counter));
  clock.seconds += kFpsCounterPeriodSeconds / 2;
  EXPECT_TRUE(fps_counter_stop(&counter));
  EXPECT_FLOAT_EQ(counter.current_fps, 2. / kFpsCounterPeriodSeconds);
}

TEST(FpsCounterTest, SmallBurstTest)
{
  ScopedTestClock clock;
  fps_counter_t   counter;

  const size_t kTickCount      = 10;
  const double kFrameIncrement = 0.15;

  // Shouldn't be enough to create a new reading on fps_counter_tick()
  ASSERT_LE(kFrameIncrement * kTickCount, kFpsCounterPeriodSeconds);

  fps_counter_start(&counter);
  for (size_t nn = 0; nn < kTickCount; ++nn)
    {
      clock.seconds += kFrameIncrement;
      EXPECT_FALSE(fps_counter_tick(&counter)) << nn << " at " << clock.seconds << " seconds.";
    }
  EXPECT_FALSE(fps_counter_stop(&counter));
}

TEST(FpsCounterTest, LongBurstTest)
{
  ScopedTestClock clock;
  fps_counter_t   counter;

  const size_t kTickCount      = 100;
  const double kFrameIncrement = 0.16;
  const double kExpectedFPS    = 1 / kFrameIncrement;

  // Should be enough to create new readings on fps_counter_tick()
  ASSERT_GE(kFrameIncrement * kTickCount, kFpsCounterPeriodSeconds);

  double threshold = kFpsCounterPeriodSeconds;

  fps_counter_start(&counter);
  for (size_t nn = 0; nn < kTickCount; ++nn)
    {
      clock.seconds += kFrameIncrement;
      if (clock.seconds >= threshold)
        {
          threshold += kFpsCounterPeriodSeconds;
          EXPECT_TRUE(fps_counter_tick(&counter)) << nn << " at " << clock.seconds << " seconds.";
        }
      else
        {
          EXPECT_FALSE(fps_counter_tick(&counter)) << nn << " at " << clock.seconds << " seconds.";
        }
    }
  EXPECT_FALSE(fps_counter_stop(&counter));
  EXPECT_FLOAT_EQ(counter.current_fps, kExpectedFPS);
}
