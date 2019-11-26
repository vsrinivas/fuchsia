// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/system-metrics/system_metrics_daemon.h"

#include <fuchsia/cobalt/cpp/fidl.h>
#include <fuchsia/cobalt/cpp/fidl_test_base.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <future>
#include <thread>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/binding_set.h"
#include "src/cobalt/bin/system-metrics/metrics_registry.cb.h"
#include "src/cobalt/bin/system-metrics/testing/fake_cpu_stats_fetcher.h"
#include "src/cobalt/bin/system-metrics/testing/fake_memory_stats_fetcher.h"
#include "src/cobalt/bin/system-metrics/testing/fake_temperature_fetcher.h"
#include "src/cobalt/bin/system-metrics/testing/fake_temperature_fetcher_not_supported.h"
#include "src/cobalt/bin/testing/fake_clock.h"
#include "src/cobalt/bin/testing/fake_logger.h"
#include "src/cobalt/bin/utils/clock.h"

using cobalt::FakeCpuStatsFetcher;
using cobalt::FakeLogger_Sync;
using cobalt::FakeMemoryStatsFetcher;
using cobalt::FakeSteadyClock;
using cobalt::FakeTemperatureFetcher;
using cobalt::FakeTemperatureFetcherNotSupported;
using cobalt::LogMethod;
using cobalt::TemperatureFetchStatus;
using fuchsia_system_metrics::FuchsiaLifetimeEventsMetricDimensionEvents;
using TimeSinceBoot =
    fuchsia_system_metrics::FuchsiaMemoryExperimental2MetricDimensionTimeSinceBoot;
using DeviceState =
    fuchsia_system_metrics::FuchsiaCpuPercentageExperimentalMetricDimensionDeviceState;
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
constexpr int32_t kTemperatureMetricBucketFloor = 0;
constexpr int32_t kTemperatureMetricNumBuckets = 80;
constexpr int32_t kTemperatureMetricStepSize = 1;
}  // namespace

class SystemMetricsDaemonTest : public gtest::TestLoopFixture {
 public:
  // Note that we first save an unprotected pointer in fake_clock_ and then
  // give ownership of the pointer to daemon_.
  SystemMetricsDaemonTest()
      : fake_clock_(new FakeSteadyClock()),
        daemon_(new SystemMetricsDaemon(
            dispatcher(), nullptr, &fake_logger_, std::unique_ptr<cobalt::SteadyClock>(fake_clock_),
            std::unique_ptr<cobalt::MemoryStatsFetcher>(new FakeMemoryStatsFetcher()),
            std::unique_ptr<cobalt::CpuStatsFetcher>(new FakeCpuStatsFetcher()),
            std::unique_ptr<cobalt::TemperatureFetcher>(new FakeTemperatureFetcher()), nullptr)) {}

  void UpdateState(fuchsia::ui::activity::State state) { daemon_->UpdateState(state); }

  seconds LogFuchsiaUpPing(seconds uptime) { return daemon_->LogFuchsiaUpPing(uptime); }

  seconds LogFuchsiaLifetimeEvents() { return daemon_->LogFuchsiaLifetimeEvents(); }

  seconds LogUpPingAndLifeTimeEvents() { return daemon_->LogUpPingAndLifeTimeEvents(); }

  seconds LogFuchsiaUptime() { return daemon_->LogFuchsiaUptime(); }

  void RepeatedlyLogUpPingAndLifeTimeEvents() {
    return daemon_->RepeatedlyLogUpPingAndLifeTimeEvents();
  }

  void RepeatedlyLogTemperature() { return daemon_->RepeatedlyLogTemperature(); }

  void LogTemperatureIfSupported(bool remaining_attempts) {
    return daemon_->LogTemperatureIfSupported(remaining_attempts);
  }

  void RepeatedlyLogUptime() { return daemon_->RepeatedlyLogUptime(); }

  void InitializeTemperatureBucketConfig(int32_t bucket_floor, int32_t num_buckets,
                                         int32_t step_size) {
    daemon_->InitializeTemperatureBucketConfig(bucket_floor, num_buckets, step_size);
  }

  // Calls BucketIndex on temperature_bucket_config_.
  uint32_t GetTemperatureBucketGivenTemperature(int64_t temperature) {
    return (daemon_->temperature_bucket_config_)->BucketIndex(temperature);
  }

  seconds LogMemoryUsage() { return daemon_->LogMemoryUsage(); }

  seconds LogCpuUsage() { return daemon_->LogCpuUsage(); }

  void PrepareForLogCpuUsage() {
    for (int i = 0; i < 59; i++) {
      daemon_->cpu_percentages_.push_back(
          {static_cast<double>(i), fuchsia::ui::activity::State::UNKNOWN});
    }
  }

  seconds LogTemperature() {
    daemon_->InitializeTemperatureBucketConfig(
        kTemperatureMetricBucketFloor, kTemperatureMetricNumBuckets, kTemperatureMetricStepSize);
    daemon_->temperature_map_[40] = 1;
    daemon_->temperature_map_[45] = 2;
    daemon_->temperature_map_[48] = 1;
    daemon_->temperature_map_[50] = 1;
    daemon_->temperature_map_size_ = 5;
    return daemon_->LogTemperature();
  }

  void SetTemperatureFetcher(std::unique_ptr<cobalt::TemperatureFetcher> fetcher) {
    daemon_->SetTemperatureFetcher(std::move(fetcher));
  }

  TimeSinceBoot GetUpTimeEventCode(const std::chrono::seconds& uptime) {
    return daemon_->GetUpTimeEventCode(uptime);
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

  void DoLogUpPingAndLifeTimeEventsTest(seconds increment_seconds, seconds expected_sleep_seconds,
                                        size_t expected_call_count,
                                        uint32_t expected_last_metric_id,
                                        uint32_t expected_last_event_code) {
    fake_logger_.reset();
    fake_clock_->Increment(increment_seconds);
    EXPECT_EQ(expected_sleep_seconds.count(), LogUpPingAndLifeTimeEvents().count());
    CheckValues(cobalt::kLogEvent, expected_call_count, expected_last_metric_id,
                expected_last_event_code);
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
  // RepeatedlyLogUpPingAndLifeTimeEvents(). It advances our two fake clocks
  // (one used by the SystemMetricDaemon, one used by the MessageLoop) by the
  // specified amount, and then checks to make sure that
  // RepeatedlyLogUpPingAndLifeTimeEvents() was executed and did the expected
  // thing.
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
  FakeSteadyClock* fake_clock_;
  FakeLogger_Sync fake_logger_;
  std::unique_ptr<SystemMetricsDaemon> daemon_;
};

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

// Tests the method LogFuchsiaLifetimeEvents(). Uses a local FakeLogger_Sync
// and does not use FIDL. Does not use the message loop.
TEST_F(SystemMetricsDaemonTest, LogFuchsiaLifetimeEvents) {
  fake_logger_.reset();
  // The first time LogFuchsiaLifetimeEvents() is invoked it should log 1
  // event of type "Boot" and return seconds::max().
  EXPECT_EQ(seconds::max(), LogFuchsiaLifetimeEvents());
  CheckValues(cobalt::kLogEvent, 1, fuchsia_system_metrics::kFuchsiaLifetimeEventsMetricId,
              FuchsiaLifetimeEventsMetricDimensionEvents::Boot);

  fake_logger_.reset();
  // The second time LogFuchsiaLifetimeEvents() is invoked it should do
  // nothing and return seconds::max().
  EXPECT_EQ(seconds::max(), LogFuchsiaLifetimeEvents());
  CheckValues(cobalt::kOther, 0, -1, -1);
}

// Tests the method LogUpPingAndLifeTimeEvents(). Uses a local FakeLogger_Sync
// and does not use FIDL. Does not use the message loop.
TEST_F(SystemMetricsDaemonTest, LogUpPingAndLifeTimeEvents) {
  // If we have been up for 1 second, expect 2 log events. First there is an
  // "Up" event and then there is "Boot" event. Expect a return value of
  // 59 seconds.
  DoLogUpPingAndLifeTimeEventsTest(seconds(1), seconds(59), 2,
                                   fuchsia_system_metrics::kFuchsiaLifetimeEventsMetricId,
                                   FuchsiaLifetimeEventsMetricDimensionEvents::Boot);

  // 59 Seconds later, expect 2 log events. First there is an "Up" event and
  // then there is an "UpOneMinute" event. Expect a return value of 9 minutes.
  DoLogUpPingAndLifeTimeEventsTest(seconds(59), minutes(9), 2,
                                   fuchsia_system_metrics::kFuchsiaUpPingMetricId,
                                   FuchsiaUpPingMetricDimensionUptime::UpOneMinute);

  // 9 Minutes minus 1 second later, expect 2 log events. First there is an
  // "Up" event and then there is an "UpOneMinute" event. Expect a return
  // value of 1 second.
  DoLogUpPingAndLifeTimeEventsTest(minutes(9) - seconds(1), seconds(1), 2,
                                   fuchsia_system_metrics::kFuchsiaUpPingMetricId,
                                   FuchsiaUpPingMetricDimensionUptime::UpOneMinute);

  // 2 seconds later, expect 3 log events. First there is an
  // "Up" event and then there is an "UpOneMinute" event and then there is an
  // "UpTenMinutes" event. Expect a return value  of 50 minutes - 1 second.
  DoLogUpPingAndLifeTimeEventsTest(seconds(2), minutes(50) - seconds(1), 3,
                                   fuchsia_system_metrics::kFuchsiaUpPingMetricId,
                                   FuchsiaUpPingMetricDimensionUptime::UpTenMinutes);

  // 50 minutes - 1 second later, the device has been up for one hour.
  // Expect 4 log events: "Up", "UpOneMinute", "UpTenMinutes", "UpOneHour".
  // Expect a return value of one hour.
  DoLogUpPingAndLifeTimeEventsTest(minutes(50) - seconds(1), hours(1), 4,
                                   fuchsia_system_metrics::kFuchsiaUpPingMetricId,
                                   FuchsiaUpPingMetricDimensionUptime::UpOneHour);

  // One hour later, the device has been up for two hours.
  // Expect 4 log events: "Up", "UpOneMinute", "UpTenMinutes", "UpOneHour".
  // Expect a return value of one hour.
  DoLogUpPingAndLifeTimeEventsTest(hours(1), hours(1), 4,
                                   fuchsia_system_metrics::kFuchsiaUpPingMetricId,
                                   FuchsiaUpPingMetricDimensionUptime::UpOneHour);

  // One hour later, the device has been up for three hours.
  // Expect 4 log events: "Up", "UpOneMinute", "UpTenMinutes", "UpOneHour".
  // Expect a return value of one hour.
  DoLogUpPingAndLifeTimeEventsTest(hours(1), hours(1), 4,
                                   fuchsia_system_metrics::kFuchsiaUpPingMetricId,
                                   FuchsiaUpPingMetricDimensionUptime::UpOneHour);

  // One hour later, the device has been up for four hours.
  // Expect 4 log events: "Up", "UpOneMinute", "UpTenMinutes", "UpOneHour".
  // Expect a return value of one hour.
  DoLogUpPingAndLifeTimeEventsTest(hours(1), hours(1), 4,
                                   fuchsia_system_metrics::kFuchsiaUpPingMetricId,
                                   FuchsiaUpPingMetricDimensionUptime::UpOneHour);

  // One hour later, the device has been up for five hours.
  // Expect 4 log events: "Up", "UpOneMinute", "UpTenMinutes", "UpOneHour".
  // Expect a return value of one hour.
  DoLogUpPingAndLifeTimeEventsTest(hours(1), hours(1), 4,
                                   fuchsia_system_metrics::kFuchsiaUpPingMetricId,
                                   FuchsiaUpPingMetricDimensionUptime::UpOneHour);

  // One hour later, the device has been up for six hours.
  // Expect 4 log events: "Up", "UpOneMinute", "UpTenMinutes", "UpOneHour".
  // Expect a return value of one hour.
  DoLogUpPingAndLifeTimeEventsTest(hours(1), hours(1), 4,
                                   fuchsia_system_metrics::kFuchsiaUpPingMetricId,
                                   FuchsiaUpPingMetricDimensionUptime::UpOneHour);

  // Six hours later, the device has been up for twelve hours.
  // Expect 5 log events: "Up", "UpOneMinute", "UpTenMinutes", "UpOneHour",
  // "UpTwelveHours". Expect a return value of one hour.
  DoLogUpPingAndLifeTimeEventsTest(hours(6), hours(1), 5,
                                   fuchsia_system_metrics::kFuchsiaUpPingMetricId,
                                   FuchsiaUpPingMetricDimensionUptime::UpTwelveHours);

  // One hour later, the device has been up for 13 hours.
  // Expect 5 log events: "Up", "UpOneMinute", "UpTenMinutes", "UpOneHour",
  // "UpTwelveHours". Expect a return value of one hour.
  DoLogUpPingAndLifeTimeEventsTest(hours(1), hours(1), 5,
                                   fuchsia_system_metrics::kFuchsiaUpPingMetricId,
                                   FuchsiaUpPingMetricDimensionUptime::UpTwelveHours);

  // One hour later, the device has been up for 14 hours.
  // Expect 5 log events: "Up", "UpOneMinute", "UpTenMinutes", "UpOneHour",
  // "UpTwelveHours". Expect a return value of one hour.
  DoLogUpPingAndLifeTimeEventsTest(hours(1), hours(1), 5,
                                   fuchsia_system_metrics::kFuchsiaUpPingMetricId,
                                   FuchsiaUpPingMetricDimensionUptime::UpTwelveHours);

  // Ten hours later, the device has been up for 24 hours.
  // Expect 6 log events: "Up", "UpOneMinute", "UpTenMinutes", "UpOneHour",
  // "UpTwelveHours", "UpOneDay". Expect a return value of one hour.
  DoLogUpPingAndLifeTimeEventsTest(hours(10), hours(1), 6,
                                   fuchsia_system_metrics::kFuchsiaUpPingMetricId,
                                   FuchsiaUpPingMetricDimensionUptime::UpOneDay);

  // One  later, the device has been up for 25 hours.
  // Expect 6 log events: "Up", "UpOneMinute", "UpTenMinutes", "UpOneHour",
  // "UpTwelveHours", "UpOneDay". Expect a return value of one hour.
  DoLogUpPingAndLifeTimeEventsTest(hours(1), hours(1), 6,
                                   fuchsia_system_metrics::kFuchsiaUpPingMetricId,
                                   FuchsiaUpPingMetricDimensionUptime::UpOneDay);
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

// Tests the method RepeatedlyLogUpPingAndLifeTimeEvents(). This test differs
// from the previous ones because it makes use of the message loop in order to
// schedule future runs of work. Uses a local FakeLogger_Sync and does not use
// FIDL.
TEST_F(SystemMetricsDaemonTest, RepeatedlyLogUpPingAndLifeTimeEvents) {
  // Make sure the loop has no initial pending work.
  RunLoopUntilIdle();

  // Invoke the method under test. This kicks of the first run and schedules
  // the second run for 1 minute plus 5 seconds in the future.
  RepeatedlyLogUpPingAndLifeTimeEvents();

  // The initial two events should have been logged, the second of which is
  // |Boot|.
  CheckValues(cobalt::kLogEvent, 2, fuchsia_system_metrics::kFuchsiaLifetimeEventsMetricId,
              FuchsiaLifetimeEventsMetricDimensionEvents::Boot);
  fake_logger_.reset();

  // Advance the clock by 30 seconds. Nothing should have happened.
  AdvanceTimeAndCheck(seconds(30), 0, -1, -1, cobalt::kLogEvent);
  // Advance the clock by 30 seconds again. Nothing should have happened
  // because the first run of RepeatedlyLogUpPingAndLifeTimeEvents() added a 5
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

// Tests the method LogMemoryUsage(). Uses a local FakeLogger_Sync and
// does not use FIDL. Does not use the message loop.
TEST_F(SystemMetricsDaemonTest, LogMemoryUsage) {
  fake_logger_.reset();
  SetClockToDaemonStartTime();
  fake_clock_->Increment(seconds(kHour * 3));
  // When LogMemoryUsage() is invoked it should log 10 events
  // for each of the memory breakdowns and return 1 minute.
  // Call count is 2, both are LogCobaltEvents.
  EXPECT_EQ(seconds(60).count(), LogMemoryUsage().count());
  CheckValues(
      cobalt::kLogCobaltEvents, 2, fuchsia_system_metrics::kFuchsiaMemoryExperimental2MetricId,
      fuchsia_system_metrics::FuchsiaMemoryExperimental2MetricDimensionMemoryBreakdown::OtherBytes,
      TimeSinceBoot::UpOneHour, 10);
}

// Tests the method LogCpuUsage(). Uses a local FakeLogger_Sync and
// does not use FIDL. Does not use the message loop.
TEST_F(SystemMetricsDaemonTest, LogCpuUsage) {
  fake_logger_.reset();
  // When LogCpuUsage() is invoked it should log 60 events
  // in 1 FIDL call, and return 1 second.
  PrepareForLogCpuUsage();
  UpdateState(fuchsia::ui::activity::State::ACTIVE);
  EXPECT_EQ(seconds(1).count(), LogCpuUsage().count());
  // Call count is 1. Just one call to LogCobaltEvents, with 60 events.
  CheckValues(cobalt::kLogCobaltEvents, 1,
              fuchsia_system_metrics::kFuchsiaCpuPercentageExperimentalMetricId,
              DeviceState::Active, -1 /*no second position event code*/, 60);

  PrepareForLogCpuUsage();
  UpdateState(fuchsia::ui::activity::State::IDLE);
  EXPECT_EQ(seconds(1).count(), LogCpuUsage().count());
  // Another call to LogCobaltEvents, with 60 events.
  CheckValues(cobalt::kLogCobaltEvents, 2,
              fuchsia_system_metrics::kFuchsiaCpuPercentageExperimentalMetricId, DeviceState::Idle,
              -1 /*no second position event code*/, 60);
}

// Tests that temperature_bucket_config_ created will put extreme temperature data into
// overflow or underflow buckets accordingly.
TEST_F(SystemMetricsDaemonTest, TemperatureBucketConfig) {
  fake_logger_.reset();
  InitializeTemperatureBucketConfig(kTemperatureMetricBucketFloor, kTemperatureMetricNumBuckets,
                                    kTemperatureMetricStepSize);

  EXPECT_EQ(GetTemperatureBucketGivenTemperature(-25), uint32_t(0));
  EXPECT_EQ(GetTemperatureBucketGivenTemperature(-1), uint32_t(0));
  EXPECT_EQ(GetTemperatureBucketGivenTemperature(0), uint32_t(1));
  EXPECT_EQ(GetTemperatureBucketGivenTemperature(1), uint32_t(2));
  EXPECT_EQ(GetTemperatureBucketGivenTemperature(79), uint32_t(80));
  EXPECT_EQ(GetTemperatureBucketGivenTemperature(80), uint32_t(81));
  EXPECT_EQ(GetTemperatureBucketGivenTemperature(100), uint32_t(81));
}

// Tests the method LogTemperature(). Uses a local FakeLogger_Sync and
// does not use FIDL. Does not use the message loop.
TEST_F(SystemMetricsDaemonTest, LogTemperature) {
  fake_logger_.reset();
  // This is already set in the constructor. Setting it again just in case people
  // change the order of tests in the future.
  SetTemperatureFetcher(std::unique_ptr<cobalt::TemperatureFetcher>(new FakeTemperatureFetcher()));
  // When LogTemperature() is invoked it should log 6 events
  // in 1 FIDL call, and return 10 second.
  EXPECT_EQ(seconds(10).count(), LogTemperature().count());
  CheckValues(cobalt::kLogIntHistogram, 1,
              fuchsia_system_metrics::kFuchsiaTemperatureExperimentalMetricId, 0);
}

// Tests first call of the method LogTemperatureIfSupported()
// when temperature fetcher returns NOT_SUPPORTED.
TEST_F(SystemMetricsDaemonTest, LogTemperatureIfSupportedNotSupported) {
  RunLoopUntilIdle();
  fake_logger_.reset();
  SetTemperatureFetcher(
      std::unique_ptr<cobalt::TemperatureFetcher>(new FakeTemperatureFetcherNotSupported()));
  LogTemperatureIfSupported(1 /*remaining_attempts*/);
  // LogTemperature would NOT be triggered.
  CheckValues(cobalt::kOther, 0, -1, -1);
  // There should be no logging activity forever.
  AdvanceTimeAndCheck(hours(24), 0, -1, -1);
}

// Tests second call of the method LogTemperatureIfSupported()
// when temperature fetcher returns NOT_SUPPORTED.
TEST_F(SystemMetricsDaemonTest, LogTemperatureIfSupportedNotSupported2) {
  RunLoopUntilIdle();
  // Second trial should give the same result.
  fake_logger_.reset();
  SetTemperatureFetcher(
      std::unique_ptr<cobalt::TemperatureFetcher>(new FakeTemperatureFetcherNotSupported()));
  LogTemperatureIfSupported(0 /*remaining_attempts*/);
  // LogTemperature would NOT be triggered.
  CheckValues(cobalt::kOther, 0, -1, -1);
  // There should be no logging activity forever.
  AdvanceTimeAndCheck(hours(24), 0, -1, -1);
}

// Tests first call of the method LogTemperatureIfSupported()
// when temperature fetcher returns SUCCEED.
TEST_F(SystemMetricsDaemonTest, LogTemperatureIfSupportedSucceed) {
  RunLoopUntilIdle();
  fake_logger_.reset();
  // SetTemperatureFetcher(std::unique_ptr<cobalt::TemperatureFetcher>(new
  // FakeTemperatureFetcher()));
  LogTemperatureIfSupported(1 /*remaining_attempts*/);
  // LogTemperature would be triggered and one cobalt call would be made within 1 minute
  AdvanceTimeAndCheck(minutes(1), 1,
                      fuchsia_system_metrics::kFuchsiaTemperatureExperimentalMetricId, 0,
                      cobalt::kLogIntHistogram);
}

// Tests second call of the method LogTemperatureIfSupported()
// when temperature fetcher returns SUCCEED.
TEST_F(SystemMetricsDaemonTest, LogTemperatureIfSupportedSucceed2) {
  RunLoopUntilIdle();
  fake_logger_.reset();
  LogTemperatureIfSupported(0 /*remaining_attempts*/);
  // LogTemperature would be triggered and one cobalt call would be made within 1 minute
  AdvanceTimeAndCheck(minutes(1), 1,
                      fuchsia_system_metrics::kFuchsiaTemperatureExperimentalMetricId, 0,
                      cobalt::kLogIntHistogram);
}

TEST_F(SystemMetricsDaemonTest, GetUpTimeEventCode) {
  EXPECT_EQ(TimeSinceBoot::UpSixDays, GetUpTimeEventCode(seconds(518400)));
  EXPECT_EQ(TimeSinceBoot::UpSixDays, GetUpTimeEventCode(seconds(600000)));
  EXPECT_EQ(TimeSinceBoot::UpThreeDays, GetUpTimeEventCode(seconds(360000)));
  EXPECT_EQ(TimeSinceBoot::UpTwoDays, GetUpTimeEventCode(seconds(172800)));
  EXPECT_EQ(TimeSinceBoot::Up, GetUpTimeEventCode(seconds(59)));
}

class MockLogger : public ::fuchsia::cobalt::testing::Logger_TestBase {
 public:
  void LogCobaltEvents(std::vector<fuchsia::cobalt::CobaltEvent> events,
                       LogCobaltEventsCallback callback) override {
    num_calls_++;
    num_events_ += events.size();
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

  seconds LogMemoryUsage() {
    // The SystemMetricsDaemon will make asynchronous calls to the MockLogger*s that are also
    // running in this class/tests thread. So the call to the SystemMetricsDaemon needs to be made
    // on a different thread, such that the MockLogger*s running on the main thread can respond to
    // those calls.
    std::future<seconds> result = std::async([this]() { return daemon_->LogMemoryUsage(); });
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
        std::unique_ptr<cobalt::MemoryStatsFetcher>(new FakeMemoryStatsFetcher()),
        std::unique_ptr<cobalt::CpuStatsFetcher>(new FakeCpuStatsFetcher()),
        std::unique_ptr<cobalt::TemperatureFetcher>(new FakeTemperatureFetcher()), nullptr));
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

  // When LogMemoryUsage() is invoked the first time, it connects to the LoggerFactory and gets a
  // logger, and returns a longer time to the next run.
  EXPECT_EQ(seconds(300).count(), LogMemoryUsage().count());

  // Make sure the Logger has now been initialized, and for the correct project, but has not yet
  // logged anything.
  EXPECT_EQ(fuchsia_system_metrics::kProjectId, logger_factory_->received_project_id());
  ASSERT_NE(nullptr, logger_factory_->logger());
  EXPECT_EQ(0, logger_factory_->logger()->num_calls());

  // Second call to LogMemoryUsage() succeeds at logging the metric, and returns a shorter time.
  EXPECT_EQ(seconds(60).count(), LogMemoryUsage().count());
  EXPECT_EQ(2, logger_factory_->logger()->num_calls());
}
