// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/system-metrics/system_metrics_daemon.h"

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/gtest/test_loop_fixture.h>

#include <future>

#include "gtest/gtest.h"
#include "src/cobalt/bin/system-metrics/fake_memory_stats_fetcher.h"
#include "src/cobalt/bin/system-metrics/metrics_registry.cb.h"
#include "src/cobalt/bin/testing/fake_clock.h"
#include "src/cobalt/bin/testing/fake_logger.h"
#include "src/cobalt/bin/utils/clock.h"

using cobalt::FakeLogger_Sync;
using cobalt::FakeMemoryStatsFetcher;
using cobalt::FakeSteadyClock;
using cobalt::LogMethod;
using fuchsia_system_metrics::FuchsiaLifetimeEventsEventCode;
using fuchsia_system_metrics::FuchsiaMemoryExperimentalEventCode;
using fuchsia_system_metrics::FuchsiaUpPingEventCode;
using std::chrono::hours;
using std::chrono::minutes;
using std::chrono::seconds;

class SystemMetricsDaemonTest : public gtest::TestLoopFixture {
 public:
  // Note that we first save an unprotected pointer in fake_clock_ and then
  // give ownership of the pointer to daemon_.
  SystemMetricsDaemonTest()
      : fake_clock_(new FakeSteadyClock()),
        daemon_(new SystemMetricsDaemon(
            dispatcher(), nullptr, &fake_logger_,
            std::unique_ptr<cobalt::SteadyClock>(fake_clock_),
            std::unique_ptr<cobalt::MemoryStatsFetcher>(
                new FakeMemoryStatsFetcher()))) {}

  seconds LogFuchsiaUpPing(seconds uptime) {
    return daemon_->LogFuchsiaUpPing(uptime);
  }

  seconds LogFuchsiaLifetimeEvents() {
    return daemon_->LogFuchsiaLifetimeEvents();
  }

  seconds LogUpTimeAndLifeTimeEvents() {
    return daemon_->LogUpTimeAndLifeTimeEvents();
  }

  void RepeatedlyLogUpTimeAndLifeTimeEvents() {
    return daemon_->RepeatedlyLogUpTimeAndLifeTimeEvents();
  }

  seconds LogMemoryUsage() { return daemon_->LogMemoryUsage(); }

  void CheckValues(LogMethod expected_log_method_invoked,
                   size_t expected_call_count, uint32_t expected_metric_id,
                   uint32_t expected_last_event_code) {
    EXPECT_EQ(expected_log_method_invoked,
              fake_logger_.last_log_method_invoked());
    EXPECT_EQ(expected_call_count, fake_logger_.call_count());
    EXPECT_EQ(expected_metric_id, fake_logger_.last_metric_id());
    EXPECT_EQ(expected_last_event_code, fake_logger_.last_event_code());
  }

  void DoFuchsiaUpPingTest(seconds now_seconds, seconds expected_sleep_seconds,
                           size_t expected_call_count,
                           uint32_t expected_last_event_code) {
    fake_logger_.reset();
    EXPECT_EQ(expected_sleep_seconds.count(),
              LogFuchsiaUpPing(now_seconds).count());
    CheckValues(cobalt::kLogEvent, expected_call_count,
                fuchsia_system_metrics::kFuchsiaUpPingMetricId,
                expected_last_event_code);
  }

  void DoLogUpTimeAndLifeTimeEventsTest(seconds increment_seconds,
                                        seconds expected_sleep_seconds,
                                        size_t expected_call_count,
                                        uint32_t expected_last_metric_id,
                                        uint32_t expected_last_event_code) {
    fake_logger_.reset();
    fake_clock_->Increment(increment_seconds);
    EXPECT_EQ(expected_sleep_seconds.count(),
              LogUpTimeAndLifeTimeEvents().count());
    CheckValues(cobalt::kLogEvent, expected_call_count, expected_last_metric_id,
                expected_last_event_code);
  }

  // This method is used by the test of the method
  // RepeatedlyLogUpTimeAndLifeTimeEvents(). It advances our two fake clocks
  // (one used by the SystemMetricDaemon, one used by the MessageLoop) by the
  // specified amount, and then checks to make sure that
  // RepeatedlyLogUpTimeAndLifeTimeEvents() was executed and did the expected
  // thing.
  void AdvanceTimeAndCheck(seconds advance_time_seconds,
                           size_t expected_call_count,
                           uint32_t expected_metric_id,
                           uint32_t expected_last_event_code) {
    bool expected_activity = (expected_call_count != 0);
    fake_clock_->Increment(advance_time_seconds);
    EXPECT_EQ(expected_activity,
              RunLoopFor(zx::sec(advance_time_seconds.count())));
    LogMethod expected_log_method_invoked =
        (expected_call_count == 0 ? cobalt::kOther : cobalt::kLogEvent);
    CheckValues(expected_log_method_invoked, expected_call_count,
                expected_metric_id, expected_last_event_code);
    fake_logger_.reset();
  }

 protected:
  FakeSteadyClock* fake_clock_;
  FakeLogger_Sync fake_logger_;
  std::unique_ptr<SystemMetricsDaemon> daemon_;
};

// Tests the method LogFuchsiaUpPing(). Uses a local FakeLogger_Sync and
// does not use FIDL. Does not use the message loop.
TEST_F(SystemMetricsDaemonTest, LogFuchsiaUpPing) {
  // If we were just booted, expect 1 log event of type "Up" and a return
  // value of 60 seconds.
  DoFuchsiaUpPingTest(seconds(0), seconds(60), 1, FuchsiaUpPingEventCode::Up);

  // If we've been up for 10 seconds, expect 1 log event of type "Up" and a
  // return value of 50 seconds.
  DoFuchsiaUpPingTest(seconds(10), seconds(50), 1, FuchsiaUpPingEventCode::Up);

  // If we've been up for 59 seconds, expect 1 log event of type "Up" and a
  // return value of 1 second.
  DoFuchsiaUpPingTest(seconds(59), seconds(1), 1, FuchsiaUpPingEventCode::Up);

  // If we've been up for 60 seconds, expect 2 log events, the second one
  // being of type UpOneMinute, and a return value of 9 minutes.
  DoFuchsiaUpPingTest(seconds(60), minutes(9), 2,
                      FuchsiaUpPingEventCode::UpOneMinute);

  // If we've been up for 61 seconds, expect 2 log events, the second one
  // being of type UpOneMinute, and a return value of 9 minutes minus 1 second.
  DoFuchsiaUpPingTest(seconds(61), minutes(9) - seconds(1), 2,
                      FuchsiaUpPingEventCode::UpOneMinute);

  // If we've been up for 10 minutes minus 1 second, expect 2 log events, the
  // second one being of type UpOneMinute, and a return value of 1 second.
  DoFuchsiaUpPingTest(minutes(10) - seconds(1), seconds(1), 2,
                      FuchsiaUpPingEventCode::UpOneMinute);

  // If we've been up for 10 minutes, expect 3 log events, the
  // last one being of type UpTenMinutes, and a return value of 50 minutes.
  DoFuchsiaUpPingTest(minutes(10), minutes(50), 3,
                      FuchsiaUpPingEventCode::UpTenMinutes);

  // If we've been up for 10 minutes plus 1 second, expect 3 log events, the
  // last one being of type UpTenMinutes, and a return value of 50 minutes minus
  // one second.
  DoFuchsiaUpPingTest(minutes(10) + seconds(1), minutes(50) - seconds(1), 3,
                      FuchsiaUpPingEventCode::UpTenMinutes);

  // If we've been up for 59 minutes, expect 3 log events, the last one being
  // of type UpTenMinutes, and a return value of 1 minute
  DoFuchsiaUpPingTest(minutes(59), minutes(1), 3,
                      FuchsiaUpPingEventCode::UpTenMinutes);

  // If we've been up for 60 minutes, expect 4 log events, the last one being
  // of type UpOneHour, and a return value of 1 hour
  DoFuchsiaUpPingTest(minutes(60), hours(1), 4,
                      FuchsiaUpPingEventCode::UpOneHour);

  // If we've been up for 61 minutes, expect 4 log events, the last one being
  // of type UpOneHour, and a return value of 1 hour
  DoFuchsiaUpPingTest(minutes(61), hours(1), 4,
                      FuchsiaUpPingEventCode::UpOneHour);

  // If we've been up for 11 hours, expect 4 log events, the last one being
  // of type UpOneHour, and a return value of 1 hour
  DoFuchsiaUpPingTest(hours(11), hours(1), 4,
                      FuchsiaUpPingEventCode::UpOneHour);

  // If we've been up for 12 hours, expect 5 log events, the last one being
  // of type UpTwelveHours, and a return value of 1 hour
  DoFuchsiaUpPingTest(hours(12), hours(1), 5,
                      FuchsiaUpPingEventCode::UpTwelveHours);

  // If we've been up for 13 hours, expect 5 log events, the last one being
  // of type UpTwelveHours, and a return value of 1 hour
  DoFuchsiaUpPingTest(hours(13), hours(1), 5,
                      FuchsiaUpPingEventCode::UpTwelveHours);

  // If we've been up for 23 hours, expect 5 log events, the last one being
  // of type UpTwelveHours, and a return value of 1 hour
  DoFuchsiaUpPingTest(hours(23), hours(1), 5,
                      FuchsiaUpPingEventCode::UpTwelveHours);

  // If we've been up for 24 hours, expect 6 log events, the last one being
  // of type UpOneDay, and a return value of 1 hour
  DoFuchsiaUpPingTest(hours(24), hours(1), 6, FuchsiaUpPingEventCode::UpOneDay);

  // If we've been up for 25 hours, expect 6 log events, the last one being
  // of type UpOneDay, and a return value of 1 hour
  DoFuchsiaUpPingTest(hours(25), hours(1), 6, FuchsiaUpPingEventCode::UpOneDay);

  // If we've been up for 250 hours, expect 6 log events, the last one being
  // of type UpOneDay, and a return value of 1 hour
  DoFuchsiaUpPingTest(hours(250), hours(1), 6,
                      FuchsiaUpPingEventCode::UpOneDay);
}

// Tests the method LogFuchsiaLifetimeEvents(). Uses a local FakeLogger_Sync and
// does not use FIDL. Does not use the message loop.
TEST_F(SystemMetricsDaemonTest, LogFuchsiaLifetimeEvents) {
  fake_logger_.reset();
  // The first time LogFuchsiaLifetimeEvents() is invoked it should log 1 event
  // of type "Boot" and return seconds::max().
  EXPECT_EQ(seconds::max(), LogFuchsiaLifetimeEvents());
  CheckValues(cobalt::kLogEvent, 1,
              fuchsia_system_metrics::kFuchsiaLifetimeEventsMetricId,
              FuchsiaLifetimeEventsEventCode::Boot);

  fake_logger_.reset();
  // The second time LogFuchsiaLifetimeEvents() is invoked it should do nothing
  // and return seconds::max().
  EXPECT_EQ(seconds::max(), LogFuchsiaLifetimeEvents());
  CheckValues(cobalt::kOther, 0, -1, -1);
}

// Tests the method LogUpTimeAndLifeTimeEvents(). Uses a local FakeLogger_Sync
// and does not use FIDL. Does not use the message loop.
TEST_F(SystemMetricsDaemonTest, LogUpTimeAndLifeTimeEvents) {
  // If we have been up for 1 second, expect 2 log events. First there is an
  // "Up" event and then there is "Boot" event. Expect a return value of
  // 59 seconds.
  DoLogUpTimeAndLifeTimeEventsTest(
      seconds(1), seconds(59), 2,
      fuchsia_system_metrics::kFuchsiaLifetimeEventsMetricId,
      FuchsiaLifetimeEventsEventCode::Boot);

  // 59 Seconds later, expect 2 log events. First there is an "Up" event and
  // then there is an "UpOneMinute" event. Expect a return value of 9 minutes.
  DoLogUpTimeAndLifeTimeEventsTest(
      seconds(59), minutes(9), 2,
      fuchsia_system_metrics::kFuchsiaUpPingMetricId,
      FuchsiaUpPingEventCode::UpOneMinute);

  // 9 Minutes minus 1 second later, expect 2 log events. First there is an
  // "Up" event and then there is an "UpOneMinute" event. Expect a return value
  // of 1 second.
  DoLogUpTimeAndLifeTimeEventsTest(
      minutes(9) - seconds(1), seconds(1), 2,
      fuchsia_system_metrics::kFuchsiaUpPingMetricId,
      FuchsiaUpPingEventCode::UpOneMinute);

  // 2 seconds later, expect 3 log events. First there is an
  // "Up" event and then there is an "UpOneMinute" event and then there is an
  // "UpTenMinutes" event. Expect a return value  of 50 minutes - 1 second.
  DoLogUpTimeAndLifeTimeEventsTest(
      seconds(2), minutes(50) - seconds(1), 3,
      fuchsia_system_metrics::kFuchsiaUpPingMetricId,
      FuchsiaUpPingEventCode::UpTenMinutes);

  // 50 minutes - 1 second later, the device has been up for one hour.
  // Expect 4 log events: "Up", "UpOneMinute", "UpTenMinutes", "UpOneHour".
  // Expect a return value of one hour.
  DoLogUpTimeAndLifeTimeEventsTest(
      minutes(50) - seconds(1), hours(1), 4,
      fuchsia_system_metrics::kFuchsiaUpPingMetricId,
      FuchsiaUpPingEventCode::UpOneHour);

  // One hour later, the device has been up for two hours.
  // Expect 4 log events: "Up", "UpOneMinute", "UpTenMinutes", "UpOneHour".
  // Expect a return value of one hour.
  DoLogUpTimeAndLifeTimeEventsTest(
      hours(1), hours(1), 4, fuchsia_system_metrics::kFuchsiaUpPingMetricId,
      FuchsiaUpPingEventCode::UpOneHour);

  // One hour later, the device has been up for three hours.
  // Expect 4 log events: "Up", "UpOneMinute", "UpTenMinutes", "UpOneHour".
  // Expect a return value of one hour.
  DoLogUpTimeAndLifeTimeEventsTest(
      hours(1), hours(1), 4, fuchsia_system_metrics::kFuchsiaUpPingMetricId,
      FuchsiaUpPingEventCode::UpOneHour);

  // One hour later, the device has been up for four hours.
  // Expect 4 log events: "Up", "UpOneMinute", "UpTenMinutes", "UpOneHour".
  // Expect a return value of one hour.
  DoLogUpTimeAndLifeTimeEventsTest(
      hours(1), hours(1), 4, fuchsia_system_metrics::kFuchsiaUpPingMetricId,
      FuchsiaUpPingEventCode::UpOneHour);

  // One hour later, the device has been up for five hours.
  // Expect 4 log events: "Up", "UpOneMinute", "UpTenMinutes", "UpOneHour".
  // Expect a return value of one hour.
  DoLogUpTimeAndLifeTimeEventsTest(
      hours(1), hours(1), 4, fuchsia_system_metrics::kFuchsiaUpPingMetricId,
      FuchsiaUpPingEventCode::UpOneHour);

  // One hour later, the device has been up for six hours.
  // Expect 4 log events: "Up", "UpOneMinute", "UpTenMinutes", "UpOneHour".
  // Expect a return value of one hour.
  DoLogUpTimeAndLifeTimeEventsTest(
      hours(1), hours(1), 4, fuchsia_system_metrics::kFuchsiaUpPingMetricId,
      FuchsiaUpPingEventCode::UpOneHour);

  // Six hours later, the device has been up for twelve hours.
  // Expect 5 log events: "Up", "UpOneMinute", "UpTenMinutes", "UpOneHour",
  // "UpTwelveHours". Expect a return value of one hour.
  DoLogUpTimeAndLifeTimeEventsTest(
      hours(6), hours(1), 5, fuchsia_system_metrics::kFuchsiaUpPingMetricId,
      FuchsiaUpPingEventCode::UpTwelveHours);

  // One hour later, the device has been up for 13 hours.
  // Expect 5 log events: "Up", "UpOneMinute", "UpTenMinutes", "UpOneHour",
  // "UpTwelveHours". Expect a return value of one hour.
  DoLogUpTimeAndLifeTimeEventsTest(
      hours(1), hours(1), 5, fuchsia_system_metrics::kFuchsiaUpPingMetricId,
      FuchsiaUpPingEventCode::UpTwelveHours);

  // One hour later, the device has been up for 14 hours.
  // Expect 5 log events: "Up", "UpOneMinute", "UpTenMinutes", "UpOneHour",
  // "UpTwelveHours". Expect a return value of one hour.
  DoLogUpTimeAndLifeTimeEventsTest(
      hours(1), hours(1), 5, fuchsia_system_metrics::kFuchsiaUpPingMetricId,
      FuchsiaUpPingEventCode::UpTwelveHours);

  // Ten hours later, the device has been up for 24 hours.
  // Expect 6 log events: "Up", "UpOneMinute", "UpTenMinutes", "UpOneHour",
  // "UpTwelveHours", "UpOneDay". Expect a return value of one hour.
  DoLogUpTimeAndLifeTimeEventsTest(
      hours(10), hours(1), 6, fuchsia_system_metrics::kFuchsiaUpPingMetricId,
      FuchsiaUpPingEventCode::UpOneDay);

  // One  later, the device has been up for 25 hours.
  // Expect 6 log events: "Up", "UpOneMinute", "UpTenMinutes", "UpOneHour",
  // "UpTwelveHours", "UpOneDay". Expect a return value of one hour.
  DoLogUpTimeAndLifeTimeEventsTest(
      hours(1), hours(1), 6, fuchsia_system_metrics::kFuchsiaUpPingMetricId,
      FuchsiaUpPingEventCode::UpOneDay);
}

// Tests the method RepeatedlyLogUpTimeAndLifeTimeEvents(). This test differs
// from the previous ones because it makes use of the message loop in order to
// schedule future runs of work. Uses a local FakeLogger_Sync and does not use
// FIDL.
TEST_F(SystemMetricsDaemonTest, RepeatedlyLogUpTimeAndLifeTimeEvents) {
  // Make sure the loop has no initial pending work.
  RunLoopUntilIdle();

  // Invoke the method under test. This kicks of the first run and schedules
  // the second run for 1 minute plus 5 seconds in the future.
  RepeatedlyLogUpTimeAndLifeTimeEvents();

  // The initial two events should have been logged, the second of which is
  // |Boot|.
  CheckValues(cobalt::kLogEvent, 2,
              fuchsia_system_metrics::kFuchsiaLifetimeEventsMetricId,
              FuchsiaLifetimeEventsEventCode::Boot);
  fake_logger_.reset();

  // Advance the clock by 30 seconds. Nothing should have happened.
  AdvanceTimeAndCheck(seconds(30), 0, -1, -1);
  // Advance the clock by 30 seconds again. Nothing should have happened
  // because the first run of RepeatedlyLogUpTimeAndLifeTimeEvents() added a 5
  // second buffer to the next scheduled run time.
  AdvanceTimeAndCheck(seconds(30), 0, -1, -1);

  // Advance the clock by 5 seconds to t=65s. Now expect the second batch
  // of work to occur. This consists of two events the second of which is
  // |UpOneMinute|. The third batch of work should be schedule for
  // t = 10m + 5s.
  AdvanceTimeAndCheck(seconds(5), 2,
                      fuchsia_system_metrics::kFuchsiaUpPingMetricId,
                      FuchsiaUpPingEventCode::UpOneMinute);

  // Advance the clock to t=10m. Nothing should have happened because the
  // previous round added a 5s buffer.
  AdvanceTimeAndCheck(minutes(10) - seconds(65), 0, -1, -1);

  // Advance the clock 5 s to t=10m + 5s. Now expect the third batch of
  // work to occur. This consists of three events the second of which is
  // |UpTenMinutes|. The fourth batch of work should be scheduled for
  // t = 1 hour + 5s.
  AdvanceTimeAndCheck(seconds(5), 3,
                      fuchsia_system_metrics::kFuchsiaUpPingMetricId,
                      FuchsiaUpPingEventCode::UpTenMinutes);

  // Advance the clock to t=1h. Nothing should have happened because the
  // previous round added a 5s buffer.
  AdvanceTimeAndCheck(minutes(60) - (minutes(10) + seconds(5)), 0, -1, -1);

  // Advance the clock 5 s to t=1h + 5s. Now expect the fourth batch of
  // work to occur. This consists of 4 events the last of which is |UpOneHour|.
  AdvanceTimeAndCheck(seconds(5), 4,
                      fuchsia_system_metrics::kFuchsiaUpPingMetricId,
                      FuchsiaUpPingEventCode::UpOneHour);
}

// Tests the method LogMemoryUsage(). Uses a local FakeLogger_Sync and
// does not use FIDL. Does not use the message loop.
TEST_F(SystemMetricsDaemonTest, LogMemoryUsage) {
  fake_logger_.reset();
  // When LogMemoryUsage() is invoked it should log 10 events
  // for each of the memory breakdowns and return 1 minute.
  EXPECT_EQ(seconds(60).count(), LogMemoryUsage().count());
  CheckValues(cobalt::kLogCobaltEvents, 2, -1, -1);
}
