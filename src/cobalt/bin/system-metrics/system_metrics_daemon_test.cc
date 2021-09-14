// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/system-metrics/system_metrics_daemon.h"

#include <fuchsia/cobalt/cpp/fidl.h>
#include <fuchsia/cobalt/cpp/fidl_test_base.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/inspect/testing/cpp/inspect.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <fstream>
#include <future>
#include <thread>

#include <gtest/gtest.h>

#include "lib/fidl/cpp/binding_set.h"
#include "src/cobalt/bin/system-metrics/metrics_registry.cb.h"
#include "src/cobalt/bin/system-metrics/testing/fake_cpu_stats_fetcher.h"
#include "src/cobalt/bin/testing/fake_clock.h"
#include "src/cobalt/bin/testing/fake_logger.h"
#include "src/cobalt/bin/utils/clock.h"

using cobalt::FakeCpuStatsFetcher;
using cobalt::FakeLogger_Sync;
using cobalt::FakeSteadyClock;
using cobalt::LogMethod;
using fuchsia_system_metrics::FuchsiaLifetimeEventsMetricDimensionEvents;
using DeviceState = fuchsia_system_metrics::CpuPercentageMetricDimensionDeviceState;
using fuchsia_system_metrics::FuchsiaUpPingMetricDimensionUptime;
using fuchsia_system_metrics::FuchsiaUptimeMetricDimensionUptimeRange;
using std::chrono::hours;
using std::chrono::milliseconds;
using std::chrono::minutes;
using std::chrono::seconds;

namespace {
typedef FuchsiaUptimeMetricDimensionUptimeRange UptimeRange;
static constexpr int kHour = 3600;
static constexpr int kDay = 24 * kHour;
static constexpr int kWeek = 7 * kDay;
}  // namespace

class SystemMetricsDaemonTest : public gtest::TestLoopFixture {
 public:
  // Note that we first save an unprotected pointer in fake_clock_ and then
  // give ownership of the pointer to daemon_.
  SystemMetricsDaemonTest()
      : executor_(dispatcher()),
        context_provider_(),
        fake_clock_(new FakeSteadyClock()),
        daemon_(new SystemMetricsDaemon(
            dispatcher(), context_provider_.context(), &fake_logger_,
            std::unique_ptr<cobalt::SteadyClock>(fake_clock_),
            std::unique_ptr<cobalt::CpuStatsFetcher>(new FakeCpuStatsFetcher()), nullptr, "tmp/")) {
    daemon_->cpu_bucket_config_ = daemon_->InitializeLinearBucketConfig(
        fuchsia_system_metrics::kCpuPercentageIntBucketsFloor,
        fuchsia_system_metrics::kCpuPercentageIntBucketsNumBuckets,
        fuchsia_system_metrics::kCpuPercentageIntBucketsStepSize);
  }

  inspect::Inspector Inspector() { return *(daemon_->inspector_.inspector()); }

  // Run a promise to completion on the default async executor.
  void RunPromiseToCompletion(fpromise::promise<> promise) {
    bool done = false;
    executor_.schedule_task(std::move(promise).and_then([&]() { done = true; }));
    RunLoopUntilIdle();
    ASSERT_TRUE(done);
  }

  fpromise::result<inspect::Hierarchy> GetHierachyFromInspect() {
    fpromise::result<inspect::Hierarchy> hierarchy;
    RunPromiseToCompletion(inspect::ReadFromInspector(Inspector())
                               .then([&](fpromise::result<inspect::Hierarchy>& result) {
                                 hierarchy = std::move(result);
                               }));
    return hierarchy;
  }

  void TearDown() {
    std::ifstream file("tmp/activation");
    if (file) {
      EXPECT_EQ(0, std::remove("tmp/activation"));
    }
  }

  void UpdateState(fuchsia::ui::activity::State state) { daemon_->UpdateState(state); }

  seconds LogFuchsiaUpPing(seconds uptime) { return daemon_->LogFuchsiaUpPing(uptime); }

  bool LogFuchsiaLifetimeEventBoot() { return daemon_->LogFuchsiaLifetimeEventBoot(); }

  bool LogFuchsiaLifetimeEventActivation() { return daemon_->LogFuchsiaLifetimeEventActivation(); }

  seconds LogFuchsiaUptime() { return daemon_->LogFuchsiaUptime(); }

  void RepeatedlyLogUpPing() { return daemon_->RepeatedlyLogUpPing(); }

  void LogLifetimeEvents() { return daemon_->LogLifetimeEvents(); }

  void LogLifetimeEventBoot() { return daemon_->LogLifetimeEventBoot(); }

  void LogLifetimeEventActivation() { return daemon_->LogLifetimeEventActivation(); }

  void RepeatedlyLogUptime() { return daemon_->RepeatedlyLogUptime(); }

  seconds LogCpuUsage() { return daemon_->LogCpuUsage(); }

  void PrepareForLogCpuUsage() {
    daemon_->cpu_data_stored_ = 599;
    daemon_->activity_state_to_cpu_map_.clear();
    daemon_->activity_state_to_cpu_map_[fuchsia::ui::activity::State::ACTIVE][345u] = 599u;
  }

  void CheckValues(LogMethod expected_log_method_invoked, size_t expected_call_count,
                   uint32_t expected_metric_id, uint32_t expected_last_event_code,
                   uint32_t expected_last_event_code_second_position = -1,
                   size_t expected_event_count = 0) {
    EXPECT_EQ(expected_log_method_invoked, fake_logger_.last_log_method_invoked());
    EXPECT_EQ(expected_call_count, fake_logger_.call_count());
    EXPECT_EQ(expected_metric_id, fake_logger_.last_metric_id());
    EXPECT_EQ(expected_last_event_code, fake_logger_.last_event_code());
    EXPECT_EQ(expected_last_event_code_second_position,
              fake_logger_.last_event_code_second_position());
    EXPECT_EQ(expected_event_count, fake_logger_.event_count());
  }

  void CheckUptimeValues(size_t expected_call_count, uint32_t expected_last_event_code,
                         int64_t expected_last_up_hours) {
    EXPECT_EQ(expected_call_count, fake_logger_.call_count());
    EXPECT_EQ(fuchsia_system_metrics::kFuchsiaUptimeMetricId, fake_logger_.last_metric_id());
    EXPECT_EQ(expected_last_event_code, fake_logger_.last_event_code());
    EXPECT_EQ(expected_last_up_hours, fake_logger_.last_elapsed_time());
  }

  void DoFuchsiaUpPingTest(seconds now_seconds, seconds expected_sleep_seconds,
                           size_t expected_call_count, uint32_t expected_last_event_code) {
    fake_logger_.reset();
    EXPECT_EQ(expected_sleep_seconds.count(), LogFuchsiaUpPing(now_seconds).count());
    CheckValues(cobalt::kLogEvent, expected_call_count,
                fuchsia_system_metrics::kFuchsiaUpPingMetricId, expected_last_event_code);
  }

  void DoFuchsiaUptimeTest(seconds now_seconds, seconds expected_sleep_seconds,
                           uint32_t expected_event_code, int64_t expected_up_hours) {
    fake_logger_.reset();
    SetClockToDaemonStartTime();
    fake_clock_->Increment(now_seconds);
    EXPECT_EQ(expected_sleep_seconds.count(), LogFuchsiaUptime().count());
    CheckUptimeValues(1u, expected_event_code, expected_up_hours);
  }

  // This method is used by the test of the method
  // RepeatedlyLogUpPing(). It advances our two fake clocks
  // (one used by the SystemMetricDaemon, one used by the MessageLoop) by the
  // specified amount, and then checks to make sure that
  // RepeatedlyLogUpPing() was executed and did the expected thing.
  void AdvanceTimeAndCheck(seconds advance_time_seconds, size_t expected_call_count,
                           uint32_t expected_metric_id, uint32_t expected_last_event_code,
                           LogMethod expected_log_method_invoked = cobalt::kOther) {
    bool expected_activity = (expected_call_count != 0);
    fake_clock_->Increment(advance_time_seconds);
    EXPECT_EQ(expected_activity, RunLoopFor(zx::sec(advance_time_seconds.count())));
    expected_log_method_invoked =
        (expected_call_count == 0 ? cobalt::kOther : expected_log_method_invoked);
    CheckValues(expected_log_method_invoked, expected_call_count, expected_metric_id,
                expected_last_event_code);
    fake_logger_.reset();
  }

  // This method is used by the test of the method RepeatedlyLogUptime(). It
  // advances our two fake clocks by the specified amount, and then checks to
  // make sure that RepeatedlyLogUptime() made the expected logging calls in the
  // meantime.
  void AdvanceAndCheckUptime(seconds advance_time_seconds, size_t expected_call_count,
                             uint32_t expected_last_event_code, int64_t expected_last_up_hours) {
    bool expected_activity = (expected_call_count != 0);
    fake_clock_->Increment(advance_time_seconds);
    EXPECT_EQ(expected_activity, RunLoopFor(zx::sec(advance_time_seconds.count())));
    if (expected_activity) {
      CheckUptimeValues(expected_call_count, expected_last_event_code, expected_last_up_hours);
    }
    fake_logger_.reset();
  }

  // Rewinds the SystemMetricsDaemon's clock back to the daemon's startup time.
  void SetClockToDaemonStartTime() { fake_clock_->set_time(daemon_->start_time_); }

 protected:
  async::Executor executor_;
  sys::testing::ComponentContextProvider context_provider_;
  FakeSteadyClock* fake_clock_;
  FakeLogger_Sync fake_logger_;
  std::unique_ptr<SystemMetricsDaemon> daemon_;
};

// Tests the method LogCpuUsage() and read from inspect
TEST_F(SystemMetricsDaemonTest, InspectCpuUsage) {
  fake_logger_.reset();
  PrepareForLogCpuUsage();
  UpdateState(fuchsia::ui::activity::State::ACTIVE);
  EXPECT_EQ(seconds(1).count(), LogCpuUsage().count());
  // Call count is 1. Just one call to LogCobaltEvents, with 60 events.
  CheckValues(cobalt::kLogCobaltEvents, 1, fuchsia_system_metrics::kCpuPercentageMetricId,
              DeviceState::Active, -1 /*no second position event code*/, 1);

  // Get hierarchy, node, and readings
  fpromise::result<inspect::Hierarchy> hierarchy = GetHierachyFromInspect();
  ASSERT_TRUE(hierarchy.is_ok());

  auto* metric_node = hierarchy.value().GetByPath({SystemMetricsDaemon::kInspecPlatformtNodeName});
  ASSERT_TRUE(metric_node);
  auto* cpu_node = metric_node->GetByPath({SystemMetricsDaemon::kCPUNodeName});
  ASSERT_TRUE(cpu_node);
  auto* cpu_max =
      cpu_node->node().get_property<inspect::DoubleArrayValue>(SystemMetricsDaemon::kReadingCPUMax);
  ASSERT_TRUE(cpu_max);

  // Expect 6 readings in the array
  EXPECT_EQ(SystemMetricsDaemon::kCPUArraySize, cpu_max->value().size());
  EXPECT_EQ(12.34, cpu_max->value()[0]);
}

// Tests the method LogFuchsiaUptime(). Uses a local FakeLogger_Sync and
// does not use FIDL. Does not use the message loop.
TEST_F(SystemMetricsDaemonTest, LogFuchsiaUptime) {
  DoFuchsiaUptimeTest(seconds(0), seconds(kHour), UptimeRange::LessThanTwoWeeks, 0);
  DoFuchsiaUptimeTest(seconds(kHour - 1), seconds(1), UptimeRange::LessThanTwoWeeks, 0);
  DoFuchsiaUptimeTest(seconds(5), seconds(kHour - 5), UptimeRange::LessThanTwoWeeks, 0);
  DoFuchsiaUptimeTest(seconds(kDay), seconds(kHour), UptimeRange::LessThanTwoWeeks, 24);
  DoFuchsiaUptimeTest(seconds(kDay + 6 * kHour + 10), seconds(kHour - 10),
                      UptimeRange::LessThanTwoWeeks, 30);
  DoFuchsiaUptimeTest(seconds(kWeek), seconds(kHour), UptimeRange::LessThanTwoWeeks, 168);
  DoFuchsiaUptimeTest(seconds(kWeek), seconds(kHour), UptimeRange::LessThanTwoWeeks, 168);
  DoFuchsiaUptimeTest(seconds(2 * kWeek), seconds(kHour), UptimeRange::TwoWeeksOrMore, 336);
  DoFuchsiaUptimeTest(seconds(2 * kWeek + 6 * kDay + 10), seconds(kHour - 10),
                      UptimeRange::TwoWeeksOrMore, 480);
}

// Tests the method LogFuchsiaUpPing(). Uses a local FakeLogger_Sync and
// does not use FIDL. Does not use the message loop.
TEST_F(SystemMetricsDaemonTest, LogFuchsiaUpPing) {
  // If we were just booted, expect 1 log event of type "Up" and a return
  // value of 60 seconds.
  DoFuchsiaUpPingTest(seconds(0), seconds(60), 1, FuchsiaUpPingMetricDimensionUptime::Up);

  // If we've been up for 10 seconds, expect 1 log event of type "Up" and a
  // return value of 50 seconds.
  DoFuchsiaUpPingTest(seconds(10), seconds(50), 1, FuchsiaUpPingMetricDimensionUptime::Up);

  // If we've been up for 59 seconds, expect 1 log event of type "Up" and a
  // return value of 1 second.
  DoFuchsiaUpPingTest(seconds(59), seconds(1), 1, FuchsiaUpPingMetricDimensionUptime::Up);

  // If we've been up for 60 seconds, expect 2 log events, the second one
  // being of type UpOneMinute, and a return value of 9 minutes.
  DoFuchsiaUpPingTest(seconds(60), minutes(9), 2, FuchsiaUpPingMetricDimensionUptime::UpOneMinute);

  // If we've been up for 61 seconds, expect 2 log events, the second one
  // being of type UpOneMinute, and a return value of 9 minutes minus 1
  // second.
  DoFuchsiaUpPingTest(seconds(61), minutes(9) - seconds(1), 2,
                      FuchsiaUpPingMetricDimensionUptime::UpOneMinute);

  // If we've been up for 10 minutes minus 1 second, expect 2 log events, the
  // second one being of type UpOneMinute, and a return value of 1 second.
  DoFuchsiaUpPingTest(minutes(10) - seconds(1), seconds(1), 2,
                      FuchsiaUpPingMetricDimensionUptime::UpOneMinute);

  // If we've been up for 10 minutes, expect 3 log events, the
  // last one being of type UpTenMinutes, and a return value of 50 minutes.
  DoFuchsiaUpPingTest(minutes(10), minutes(50), 3,
                      FuchsiaUpPingMetricDimensionUptime::UpTenMinutes);

  // If we've been up for 10 minutes plus 1 second, expect 3 log events, the
  // last one being of type UpTenMinutes, and a return value of 50 minutes
  // minus one second.
  DoFuchsiaUpPingTest(minutes(10) + seconds(1), minutes(50) - seconds(1), 3,
                      FuchsiaUpPingMetricDimensionUptime::UpTenMinutes);

  // If we've been up for 59 minutes, expect 3 log events, the last one being
  // of type UpTenMinutes, and a return value of 1 minute
  DoFuchsiaUpPingTest(minutes(59), minutes(1), 3, FuchsiaUpPingMetricDimensionUptime::UpTenMinutes);

  // If we've been up for 60 minutes, expect 4 log events, the last one being
  // of type UpOneHour, and a return value of 1 hour
  DoFuchsiaUpPingTest(minutes(60), hours(1), 4, FuchsiaUpPingMetricDimensionUptime::UpOneHour);

  // If we've been up for 61 minutes, expect 4 log events, the last one being
  // of type UpOneHour, and a return value of 1 hour
  DoFuchsiaUpPingTest(minutes(61), hours(1), 4, FuchsiaUpPingMetricDimensionUptime::UpOneHour);

  // If we've been up for 11 hours, expect 4 log events, the last one being
  // of type UpOneHour, and a return value of 1 hour
  DoFuchsiaUpPingTest(hours(11), hours(1), 4, FuchsiaUpPingMetricDimensionUptime::UpOneHour);

  // If we've been up for 12 hours, expect 5 log events, the last one being
  // of type UpTwelveHours, and a return value of 1 hour
  DoFuchsiaUpPingTest(hours(12), hours(1), 5, FuchsiaUpPingMetricDimensionUptime::UpTwelveHours);

  // If we've been up for 13 hours, expect 5 log events, the last one being
  // of type UpTwelveHours, and a return value of 1 hour
  DoFuchsiaUpPingTest(hours(13), hours(1), 5, FuchsiaUpPingMetricDimensionUptime::UpTwelveHours);

  // If we've been up for 23 hours, expect 5 log events, the last one being
  // of type UpTwelveHours, and a return value of 1 hour
  DoFuchsiaUpPingTest(hours(23), hours(1), 5, FuchsiaUpPingMetricDimensionUptime::UpTwelveHours);

  // If we've been up for 24 hours, expect 6 log events, the last one being
  // of type UpOneDay, and a return value of 1 hour
  DoFuchsiaUpPingTest(hours(24), hours(1), 6, FuchsiaUpPingMetricDimensionUptime::UpOneDay);

  // If we've been up for 25 hours, expect 6 log events, the last one being
  // of type UpOneDay, and a return value of 1 hour
  DoFuchsiaUpPingTest(hours(25), hours(1), 6, FuchsiaUpPingMetricDimensionUptime::UpOneDay);

  // If we've been up for 73 hours, expect 7 log events, the last one being
  // of type UpOneDay, and a return value of 1 hour
  DoFuchsiaUpPingTest(hours(73), hours(1), 7, FuchsiaUpPingMetricDimensionUptime::UpThreeDays);

  // If we've been up for 250 hours, expect 8 log events, the last one being
  // of type UpSixDays, and a return value of 1 hour
  DoFuchsiaUpPingTest(hours(250), hours(1), 8, FuchsiaUpPingMetricDimensionUptime::UpSixDays);
}

// Tests the method LogFuchsiaLifetimeEventBoot(). Uses a local FakeLogger_Sync
// and does not use FIDL. Does not use the message loop.
TEST_F(SystemMetricsDaemonTest, LogFuchsiaLifetimeEventBoot) {
  fake_logger_.reset();

  // The first time LogFuchsiaLifetimeEventBoot() is invoked it should log 1
  // event of type "Boot" and return true indicating a successful status.
  EXPECT_EQ(true, LogFuchsiaLifetimeEventBoot());
  CheckValues(cobalt::kLogEvent, 1, fuchsia_system_metrics::kFuchsiaLifetimeEventsMetricId,
              FuchsiaLifetimeEventsMetricDimensionEvents::Boot);
}

// Tests the method LogFuchsiaLifetimeEventActivation(). Uses a local FakeLogger_Sync
// and does not use FIDL. Does not use the message loop.
TEST_F(SystemMetricsDaemonTest, LogFuchsiaLifetimeEventActivation) {
  fake_logger_.reset();
  // The first time LogFuchsiaLifetimeEventActivation() is invoked it should log 1
  // event of type "Activation" and return true indicating a successful status.
  EXPECT_EQ(true, LogFuchsiaLifetimeEventActivation());
  CheckValues(cobalt::kLogEvent, 1, fuchsia_system_metrics::kFuchsiaLifetimeEventsMetricId,
              FuchsiaLifetimeEventsMetricDimensionEvents::Activation);
  fake_logger_.reset();

  // The second time LogFuchsiaLifetimeEventActivation() it should log zero events
  // and return true to indicate successful completion.
  EXPECT_EQ(true, LogFuchsiaLifetimeEventActivation());
  CheckValues(cobalt::kOther, 0, -1, -1);
}

// Tests the method RepeatedlyLogUptime(). This test uses the message loop to
// schedule future runs of work. Uses a local FakeLogger_Sync and does not use
// FIDL.
TEST_F(SystemMetricsDaemonTest, RepeatedlyLogUptime) {
  RunLoopUntilIdle();

  // Invoke the method under test. This should cause the uptime to be logged
  // once, and schedules the next run for approximately 1 hour in the future.
  // (More precisely, the next run should occur in 1 hour minus the amount of
  // time after the daemon's start time which this method is invoked.)
  RepeatedlyLogUptime();

  // The first event should have been logged, with an uptime of 0 hours.
  CheckUptimeValues(1u, UptimeRange::LessThanTwoWeeks, 0);
  fake_logger_.reset();

  // Advance the clock by 30 seconds. Nothing should have happened.
  AdvanceAndCheckUptime(seconds(30), 0, -1, -1);

  // Advance the clock to the next hour. The system metrics daemon has been up
  // for 1 hour by now, so the second event should have been logged.
  AdvanceAndCheckUptime(seconds(kHour - 30), 1, UptimeRange::LessThanTwoWeeks, 1);

  // Advance the clock by 1 day. At this point, the daemon has been up for 25
  // hours. Since the last time we checked |fake_logger_|, the daemon should
  // have logged the uptime 24 times, with the most recent value equal to 25.
  AdvanceAndCheckUptime(seconds(kDay), 24, UptimeRange::LessThanTwoWeeks, 25);

  // Advance the clock by 1 week. At this point, the daemon has been up for 8
  // days + 1 hour. Since the last time we checked |fake_logger_|, the daemon
  // should have logged the uptime 168 times, with the most recent value equal
  // to 193.
  AdvanceAndCheckUptime(seconds(kWeek), 168, UptimeRange::LessThanTwoWeeks, 193);

  // Advance the clock 1 more week. At this point, the daemon has been up for
  // 15 days + 1 hour. Since the last time we checked |fake_logger_|, the daemon
  // should have logged the uptime 168 times, with the most recent value equal
  // to 361.
  AdvanceAndCheckUptime(seconds(kWeek), 168, UptimeRange::TwoWeeksOrMore, 361);
}

// Tests the method RepeatedlyLogUpPing(). This test differs
// from the previous ones because it makes use of the message loop in order to
// schedule future runs of work. Uses a local FakeLogger_Sync and does not use
// FIDL.
TEST_F(SystemMetricsDaemonTest, RepeatedlyLogUpPing) {
  // Make sure the loop has no initial pending work.
  RunLoopUntilIdle();

  // Invoke the method under test. This kicks of the first run and schedules
  // the second run for 1 minute plus 5 seconds in the future.
  RepeatedlyLogUpPing();

  // The initial event should have been logged.
  CheckValues(cobalt::kLogEvent, 1, fuchsia_system_metrics::kFuchsiaUpPingMetricId,
              FuchsiaUpPingMetricDimensionUptime::Up);
  fake_logger_.reset();

  // Advance the clock by 30 seconds. Nothing should have happened.
  AdvanceTimeAndCheck(seconds(30), 0, -1, -1, cobalt::kLogEvent);
  // Advance the clock by 30 seconds again. Nothing should have happened
  // because the first run of RepeatedlyLogUpPing() added a 5
  // second buffer to the next scheduled run time.
  AdvanceTimeAndCheck(seconds(30), 0, -1, -1, cobalt::kLogEvent);

  // Advance the clock by 5 seconds to t=65s. Now expect the second batch
  // of work to occur. This consists of two events the second of which is
  // |UpOneMinute|. The third batch of work should be schedule for
  // t = 10m + 5s.
  AdvanceTimeAndCheck(seconds(5), 2, fuchsia_system_metrics::kFuchsiaUpPingMetricId,
                      FuchsiaUpPingMetricDimensionUptime::UpOneMinute, cobalt::kLogEvent);

  // Advance the clock to t=10m. Nothing should have happened because the
  // previous round added a 5s buffer.
  AdvanceTimeAndCheck(minutes(10) - seconds(65), 0, -1, -1, cobalt::kLogEvent);

  // Advance the clock 5 s to t=10m + 5s. Now expect the third batch of
  // work to occur. This consists of three events the second of which is
  // |UpTenMinutes|. The fourth batch of work should be scheduled for
  // t = 1 hour + 5s.
  AdvanceTimeAndCheck(seconds(5), 3, fuchsia_system_metrics::kFuchsiaUpPingMetricId,
                      FuchsiaUpPingMetricDimensionUptime::UpTenMinutes, cobalt::kLogEvent);

  // Advance the clock to t=1h. Nothing should have happened because the
  // previous round added a 5s buffer.
  AdvanceTimeAndCheck(minutes(60) - (minutes(10) + seconds(5)), 0, -1, -1, cobalt::kLogEvent);

  // Advance the clock 5 s to t=1h + 5s. Now expect the fourth batch of
  // work to occur. This consists of 4 events the last of which is
  // |UpOneHour|.
  AdvanceTimeAndCheck(seconds(5), 4, fuchsia_system_metrics::kFuchsiaUpPingMetricId,
                      FuchsiaUpPingMetricDimensionUptime::UpOneHour, cobalt::kLogEvent);
}

// Tests the method LogLifetimeEvents(). This test differs
// from the previous ones because it makes use of the message loop in order to
// schedule future runs of work. Uses a local FakeLogger_Sync and does not use
// FIDL.
TEST_F(SystemMetricsDaemonTest, LogLifetimeEvents) {
  // Make sure the loop has no initial pending work.
  RunLoopUntilIdle();

  // Invoke the method under test. This kicks of the first run and schedules the
  // second run.
  LogLifetimeEvents();

  // Two initial events should be logged, one for Activation and one for Boot.
  // Activation is the last event logged.
  CheckValues(cobalt::kLogEvent, 2, fuchsia_system_metrics::kFuchsiaLifetimeEventsMetricId,
              FuchsiaLifetimeEventsMetricDimensionEvents::Activation);
}

// Tests the method LogLifetimeEventActivation(). This test differs
// from the previous ones because it makes use of the message loop in order to
// schedule future runs of work. Uses a local FakeLogger_Sync and does not use
// FIDL.
TEST_F(SystemMetricsDaemonTest, LogLifetimeEventActivation) {
  // Make sure the loop has no initial pending work.
  RunLoopUntilIdle();

  // Invoke the method under test. This kicks of the first run and schedules the
  // second run.
  LogLifetimeEventActivation();

  // The initial event should have been logged.
  CheckValues(cobalt::kLogEvent, 1, fuchsia_system_metrics::kFuchsiaLifetimeEventsMetricId,
              FuchsiaLifetimeEventsMetricDimensionEvents::Activation);
  fake_logger_.reset();

  // Advance the clock by 2 hours. Nothing should have happened.
  AdvanceTimeAndCheck(hours(2), 0, -1, -1, cobalt::kLogEvent);
}

// Tests the method LogLifetimeEventBoot(). This test differs
// from the previous ones because it makes use of the message loop in order to
// schedule future runs of work. Uses a local FakeLogger_Sync and does not use
// FIDL.
TEST_F(SystemMetricsDaemonTest, LogLifetimeEventBoot) {
  // Make sure the loop has no initial pending work.
  RunLoopUntilIdle();

  // Invoke the method under test. This kicks of the first run and schedules the
  // second run.
  LogLifetimeEventBoot();

  // The initial event should have been logged.
  CheckValues(cobalt::kLogEvent, 1, fuchsia_system_metrics::kFuchsiaLifetimeEventsMetricId,
              FuchsiaLifetimeEventsMetricDimensionEvents::Boot);
  fake_logger_.reset();

  // Advance the clock by 2 hours. Nothing should have happened.
  AdvanceTimeAndCheck(hours(2), 0, -1, -1, cobalt::kLogEvent);
}

// Tests the method LogCpuUsage(). Uses a local FakeLogger_Sync and
// does not use FIDL. Does not use the message loop.
TEST_F(SystemMetricsDaemonTest, LogCpuUsage) {
  fake_logger_.reset();
  PrepareForLogCpuUsage();
  UpdateState(fuchsia::ui::activity::State::ACTIVE);
  EXPECT_EQ(seconds(1).count(), LogCpuUsage().count());
  // Call count is 1. Just one call to LogCobaltEvents, with 60 events.
  CheckValues(cobalt::kLogCobaltEvents, 1, fuchsia_system_metrics::kCpuPercentageMetricId,
              DeviceState::Active, -1 /*no second position event code*/, 1);
}

class MockLogger : public ::fuchsia::cobalt::testing::Logger_TestBase {
 public:
  void LogCobaltEvents(std::vector<fuchsia::cobalt::CobaltEvent> events,
                       LogCobaltEventsCallback callback) override {
    num_calls_++;
    num_events_ += events.size();
    callback(fuchsia::cobalt::Status::OK);
  }
  void LogEvent(uint32_t metric_id, uint32_t event_code,
                LogCobaltEventsCallback callback) override {
    num_calls_++;
    num_events_ += 1;
    callback(fuchsia::cobalt::Status::OK);
  }
  void NotImplemented_(const std::string& name) override {
    ASSERT_TRUE(false) << name << " is not implemented";
  }
  int num_calls() { return num_calls_; }
  int num_events() { return num_events_; }

 private:
  int num_calls_ = 0;
  int num_events_ = 0;
};

class MockLoggerFactory : public ::fuchsia::cobalt::testing::LoggerFactory_TestBase {
 public:
  MockLogger* logger() { return logger_.get(); }
  uint32_t received_project_id() { return received_project_id_; }

  void CreateLoggerFromProjectId(uint32_t project_id,
                                 ::fidl::InterfaceRequest<fuchsia::cobalt::Logger> logger,
                                 CreateLoggerFromProjectIdCallback callback) override {
    received_project_id_ = project_id;
    logger_.reset(new MockLogger());
    logger_bindings_.AddBinding(logger_.get(), std::move(logger));
    callback(fuchsia::cobalt::Status::OK);
  }

  void NotImplemented_(const std::string& name) override {
    ASSERT_TRUE(false) << name << " is not implemented";
  }

 private:
  uint32_t received_project_id_;
  std::unique_ptr<MockLogger> logger_;
  fidl::BindingSet<fuchsia::cobalt::Logger> logger_bindings_;
};

class SystemMetricsDaemonInitializationTest : public gtest::TestLoopFixture {
 public:
  ~SystemMetricsDaemonInitializationTest() override = default;

  bool LogFuchsiaLifetimeEvent() {
    // The SystemMetricsDaemon will make asynchronous calls to the MockLogger*s that are also
    // running in this class/tests thread. So the call to the SystemMetricsDaemon needs to be made
    // on a different thread, such that the MockLogger*s running on the main thread can respond to
    // those calls.
    std::future<bool> result =
        std::async([this]() { return daemon_->LogFuchsiaLifetimeEventBoot(); });
    while (result.wait_for(milliseconds(1)) != std::future_status::ready) {
      // Run the main thread's loop, allowing the MockLogger* objects to respond to requests.
      RunLoopUntilIdle();
    }
    return result.get();
  }

 protected:
  void SetUp() override {
    // Create a MockLoggerFactory and add it to the services the fake context can provide.
    auto service_provider = context_provider_.service_directory_provider();
    logger_factory_ = new MockLoggerFactory();
    service_provider->AddService(factory_bindings_.GetHandler(logger_factory_, dispatcher()));

    // Initialize the SystemMetricsDaemon with the fake context, and other fakes.
    daemon_ = std::unique_ptr<SystemMetricsDaemon>(new SystemMetricsDaemon(
        dispatcher(), context_provider_.context(), nullptr,
        std::unique_ptr<cobalt::SteadyClock>(fake_clock_),
        std::unique_ptr<cobalt::CpuStatsFetcher>(new FakeCpuStatsFetcher()), nullptr, "/tmp"));
  }

  // Note that we first save an unprotected pointer in fake_clock_ and then
  // give ownership of the pointer to daemon_.
  FakeSteadyClock* fake_clock_ = new FakeSteadyClock();
  std::unique_ptr<SystemMetricsDaemon> daemon_;

  MockLoggerFactory* logger_factory_;
  fidl::BindingSet<fuchsia::cobalt::LoggerFactory> factory_bindings_;
  sys::testing::ComponentContextProvider context_provider_;
};

// Tests the initialization of a new SystemMetricsDaemon's connection to the Cobalt FIDL objects.
TEST_F(SystemMetricsDaemonInitializationTest, LogSomethingAnything) {
  // Make sure the Logger has not been initialized yet.
  EXPECT_EQ(0u, logger_factory_->received_project_id());
  EXPECT_EQ(nullptr, logger_factory_->logger());

  // When LogFuchsiaLifetimeEvent() is invoked the first time, it connects to the LoggerFactory,
  // gets a logger, and returns false to indicate the logging failed and should
  // be retried.
  EXPECT_EQ(false, LogFuchsiaLifetimeEvent());

  // Make sure the Logger has now been initialized, and for the correct project, but has not yet
  // logged anything.
  EXPECT_EQ(fuchsia_system_metrics::kProjectId, logger_factory_->received_project_id());
  ASSERT_NE(nullptr, logger_factory_->logger());
  EXPECT_EQ(0, logger_factory_->logger()->num_calls());

  // Second call to LogFuchsiaLifetimeEvent() succeeds at logging the metric, and returns
  // success.
  EXPECT_EQ(true, LogFuchsiaLifetimeEvent());
  EXPECT_EQ(1, logger_factory_->logger()->num_calls());
}
