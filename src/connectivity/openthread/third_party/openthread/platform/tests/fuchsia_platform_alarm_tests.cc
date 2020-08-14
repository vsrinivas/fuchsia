// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/time.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/connectivity/openthread/third_party/openthread/platform/fuchsia_platform_alarm.h"

static constexpr uint64_t kNanoSecondsPerMicroSecond = 1000;

static uint64_t GetCurTimeMicroSec() {
  uint64_t cur_time_ns = static_cast<uint64_t>(zx_clock_get_monotonic());
  return (cur_time_ns / kNanoSecondsPerMicroSecond);
}

TEST(Alarm, MilliSecAlarm) {
  FuchsiaPlatformAlarm alarm;
  alarm.SetSpeedUpFactor(1);

  constexpr uint32_t kMsDelay = 100;

  uint32_t now = alarm.GetNowMilliSec();
  alarm.SetMilliSecAlarm(now + kMsDelay);

  EXPECT_EQ(alarm.MilliSecAlarmFired(), false);

  // Sleeping for duration of alarm ensures slightly extra time
  // due to computation time. Hence alarm should have fired by then
  zx::nanosleep(zx::deadline_after(zx::msec(kMsDelay)));
  EXPECT_EQ(alarm.MilliSecAlarmFired(), true);
}

// Test for clear alarm:
TEST(Alarm, ClearMilliSecAlarm) {
  FuchsiaPlatformAlarm alarm;
  alarm.SetSpeedUpFactor(1);

  constexpr uint32_t kMsDelay = 100;
  uint32_t now = alarm.GetNowMilliSec();
  alarm.SetMilliSecAlarm(now + kMsDelay);
  EXPECT_EQ(alarm.MilliSecAlarmFired(), false);
  alarm.ClearMilliSecAlarm();

  // Sleep for long enough duration in which alarm would have
  // definitely fired (if it weren't cancelled)
  zx::nanosleep(zx::deadline_after(zx::msec(kMsDelay * 2)));
  EXPECT_EQ(alarm.MilliSecAlarmFired(), false);
}

// Test for remaining time:
TEST(Alarm, RemainingTimeMilliSecAlarm) {
  FuchsiaPlatformAlarm alarm;
  alarm.SetSpeedUpFactor(1);

  // Outer bound starts before setting alarm, and ends after getting remaining
  // time
  uint64_t start_time_outer_bound_us = GetCurTimeMicroSec();

  constexpr uint32_t kMsAlarmDelay = 1000;
  constexpr uint32_t kMsSleepDuration = 200;
  uint32_t now = alarm.GetNowMilliSec();
  alarm.SetMilliSecAlarm(now + kMsAlarmDelay);

  // Inner bound starts after setting alarm and ends before getting remaining
  // time
  uint64_t start_time_inner_bound_us = GetCurTimeMicroSec();

  zx::nanosleep(zx::deadline_after(zx::msec(kMsSleepDuration)));

  uint64_t end_time_inner_bound_us = GetCurTimeMicroSec();

  // This is the function under test here:
  uint32_t remaining_time_us = alarm.GetRemainingTimeMicroSec();
  uint32_t remaining_time_ms = alarm.MicroToMilliSec(remaining_time_us);

  uint64_t end_time_outer_bound_us = GetCurTimeMicroSec();

  // Get the execution times - two values to create a range for expected value
  uint64_t execution_time_inner_bound_ms =
      alarm.MicroToMilliSec(end_time_inner_bound_us - start_time_inner_bound_us);
  uint64_t execution_time_outer_bound_ms =
      alarm.MicroToMilliSec(end_time_outer_bound_us - start_time_outer_bound_us);

  // Note:
  // 1) Due to rounding involved at various places during division, it
  // is possible that execution time outer bound is smaller than remaining time.
  // For example, alarm is actually set after 999500 micro seconds. So even if
  // sleep duration is exactly 200 ms, and outer bound comes to 200ms, the
  // remaining time will be 799ms.
  // 2) Moreover, if execution time is slightly more than 200ms, say 200500
  // micro-seconds. The expected remaining time if calculated as
  // (AlarmDelay - execution_time_outer_bound_ms)
  // will still be 800 ms. But actual remaining time will go down to 798 ms.
  // Both these rounding errors can cause and have been found to cause mismatch
  // between expected and actual remaining time. Give some margin to account for
  // these and similar rounding errors:
  constexpr uint32_t kMarginForRoundingMs = 2;
  uint32_t expected_remaining_time_min_ms = kMsAlarmDelay - execution_time_outer_bound_ms;
  expected_remaining_time_min_ms -= kMarginForRoundingMs;
  uint32_t expected_remaining_time_max_ms = kMsAlarmDelay - execution_time_inner_bound_ms;
  expected_remaining_time_max_ms += kMarginForRoundingMs;

  if (kMsAlarmDelay > execution_time_outer_bound_ms + kMarginForRoundingMs) {
    // Alarm definitely not expired
    EXPECT_LE(remaining_time_ms, expected_remaining_time_max_ms);
    EXPECT_GE(remaining_time_ms, expected_remaining_time_min_ms);
  }
}

TEST(Alarm, MicroSecAlarm) {
  FuchsiaPlatformAlarm alarm;
  alarm.SetSpeedUpFactor(1);

  uint32_t now = alarm.GetNowMicroSec();
  constexpr uint32_t kUsAlarmDelay = 10000;
  alarm.SetMicroSecAlarm(now + kUsAlarmDelay);
  EXPECT_EQ(alarm.MicroSecAlarmFired(), false);
  zx::nanosleep(zx::deadline_after(zx::usec(kUsAlarmDelay)));
  EXPECT_EQ(alarm.MicroSecAlarmFired(), true);
}

// Setup both alarms at once, one after 500 msec
// another after 1000 sec. Check at 750 msec and 1200 msec
// to ensure correct alarms are fired
TEST(Alarm, BothAlarms) {
  FuchsiaPlatformAlarm alarm;
  alarm.SetSpeedUpFactor(1);

  uint64_t start_time_us = GetCurTimeMicroSec();

  uint32_t now_us = alarm.GetNowMicroSec();
  constexpr uint32_t kUsAlarmDelay = 500000;
  alarm.SetMicroSecAlarm(now_us + kUsAlarmDelay);
  uint32_t now_ms = alarm.GetNowMilliSec();
  constexpr uint32_t kMsAlarmDelay = 1000;
  alarm.SetMilliSecAlarm(now_ms + kMsAlarmDelay);
  EXPECT_EQ(alarm.MicroSecAlarmFired(), false);

  constexpr uint32_t kMsSleepDelay1 = 750;
  constexpr uint32_t kMsSleepDelay2 = 450;
  // Sleep for some time to wake up after first alarms expected
  // time to fire and before second alarm's expected time to fire
  zx::nanosleep(zx::deadline_after(zx::msec(kMsSleepDelay1)));
  EXPECT_EQ(alarm.MicroSecAlarmFired(), true);

  // In some cases, the actual sleep time may be much longer than
  // requested. Check Ms delay has already passed
  uint64_t end_time_us = GetCurTimeMicroSec();
  if (end_time_us - start_time_us >
        alarm.MilliToMicroSec( kMsAlarmDelay)) {
    EXPECT_EQ(alarm.MilliSecAlarmFired(), true);
    return;
  }

  EXPECT_EQ(alarm.MilliSecAlarmFired(), false);
  zx::nanosleep(zx::deadline_after(zx::msec(kMsSleepDelay2)));
  EXPECT_EQ(alarm.MilliSecAlarmFired(), true);
}

TEST(Alarm, ClearMicroSecAlarm) {
  FuchsiaPlatformAlarm alarm;
  alarm.SetSpeedUpFactor(1);

  uint32_t now = alarm.GetNowMicroSec();
  uint32_t kUsAlarmDelay = 5000;
  alarm.SetMicroSecAlarm(now + kUsAlarmDelay);
  EXPECT_EQ(alarm.MicroSecAlarmFired(), false);
  alarm.ClearMicroSecAlarm();

  // Sleep for long enough duration in which alarm would have
  // definitely fired (if it weren't cancelled)
  zx::nanosleep(zx::deadline_after(zx::usec(kUsAlarmDelay * 2)));
  EXPECT_EQ(alarm.MicroSecAlarmFired(), false);
}

TEST(Alarm, RemainingTimeMicroSecAlarm) {
  FuchsiaPlatformAlarm alarm;
  alarm.SetSpeedUpFactor(1);

  // Outer bound starts before setting alarm and ends after getting
  // the remaining time
  uint64_t start_time_outer_bound_us = GetCurTimeMicroSec();

  uint32_t now = alarm.GetNowMicroSec();
  constexpr uint32_t kUsAlarmDelay = 1000000;    // 1 sec
  constexpr uint32_t kUsSleepDuration = 100000;  // 0.1 sec

  // Inner bound starts after setting alarm and ends before getting
  // the remaining time
  uint64_t start_time_inner_bound_us = GetCurTimeMicroSec();

  alarm.SetMicroSecAlarm(now + kUsAlarmDelay);
  zx::nanosleep(zx::deadline_after(zx::usec(kUsSleepDuration)));

  uint64_t end_time_inner_bound_us = GetCurTimeMicroSec();

  uint32_t remaining_time_us = alarm.GetRemainingTimeMicroSec();

  uint64_t end_time_outer_bound_us = GetCurTimeMicroSec();

  // Keep some time margin to account for time measurement itself taking time
  constexpr uint32_t kMarginUs = 10;
  uint64_t execution_time_inner_bound_us =
      end_time_inner_bound_us - start_time_inner_bound_us - kMarginUs;
  uint64_t execution_time_outer_bound_us =
      end_time_outer_bound_us - start_time_outer_bound_us + kMarginUs;

  uint32_t expected_remaining_time_min_us = kUsAlarmDelay - execution_time_outer_bound_us;
  uint32_t expected_remaining_time_max_us = kUsAlarmDelay - execution_time_inner_bound_us;

  if (kUsAlarmDelay >= execution_time_outer_bound_us) {
    // Alarm didn't expire. Normal testing scenario.
    // Verify that the actual remaining time should be within expected range
    EXPECT_LE(remaining_time_us, expected_remaining_time_max_us);
    EXPECT_GE(remaining_time_us, expected_remaining_time_min_us);
  } else {
    // Alarm may have expired. This case is expected to be rare.
    // The real intent of the test is to have timer not expired
    if (kUsAlarmDelay > execution_time_inner_bound_us) {
      // This is indeterminate case, we don't really know
      // whether alarm had expired before or after the call to
      // Get Remaining time
    } else {
      // Alarm definitely expired, so remaining time reported must be zero
      constexpr uint32_t kExpectedRemainingTimeAlarmFired = 0;
      EXPECT_EQ(remaining_time_us, kExpectedRemainingTimeAlarmFired);
    }
  }
}

// In presence of both alarms, the smaller one must be reported
TEST(Alarm, RemainingTimeBothAlarms) {
  FuchsiaPlatformAlarm alarm;
  alarm.SetSpeedUpFactor(1);

  // Outer bound starts before setting alarm and ends after getting
  // the remaining time
  uint64_t start_time_outer_bound_us = GetCurTimeMicroSec();

  uint32_t now_ms = alarm.GetNowMilliSec();
  constexpr uint32_t kMsAlarmDelay = 200;  // 0.2 sec
  alarm.SetMilliSecAlarm(now_ms + kMsAlarmDelay);

  uint32_t now_us = alarm.GetNowMicroSec();
  constexpr uint32_t kUsAlarmDelay = 100000;    // 0.1 sec
  constexpr uint32_t kUsSleepDuration = 40000;  // 0.04 sec
  alarm.SetMicroSecAlarm(now_us + kUsAlarmDelay);

  // Inner bound starts after setting alarm and ends before getting
  // the remaining time
  uint64_t start_time_inner_bound_us = GetCurTimeMicroSec();

  zx::nanosleep(zx::deadline_after(zx::usec(kUsSleepDuration)));

  uint64_t end_time_inner_bound_us = GetCurTimeMicroSec();

  uint32_t remaining_time_us = alarm.GetRemainingTimeMicroSec();

  // End time:
  uint64_t end_time_outer_bound_us = GetCurTimeMicroSec();

  // Keep some time margin to account for time measurement itself taking time
  constexpr uint32_t kMarginUs = 10;
  uint64_t execution_time_inner_bound_us =
      end_time_inner_bound_us - start_time_inner_bound_us - kMarginUs;
  uint64_t execution_time_outer_bound_us =
      end_time_outer_bound_us - start_time_outer_bound_us + kMarginUs;

  uint32_t expected_remaining_time_min_us = kUsAlarmDelay - execution_time_outer_bound_us;
  uint32_t expected_remaining_time_max_us = kUsAlarmDelay - execution_time_inner_bound_us;

  if (kUsAlarmDelay >= execution_time_outer_bound_us) {
    // Alarm didn't expire. Normal testing scenario.
    // Verify that the actual remaining time should be within expected range
    EXPECT_LE(remaining_time_us, expected_remaining_time_max_us);
    EXPECT_GE(remaining_time_us, expected_remaining_time_min_us);
  }
}
