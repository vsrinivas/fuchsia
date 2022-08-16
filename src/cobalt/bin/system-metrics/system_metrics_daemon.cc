// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The cobalt system metrics collection daemon uses cobalt to log system metrics
// on a regular basis.
#include "src/cobalt/bin/system-metrics/system_metrics_daemon.h"

#include <fcntl.h>
#include <fuchsia/metrics/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/sys/inspect/cpp/component.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/zx/resource.h>
#include <sys/statvfs.h>
#include <zircon/status.h>

#include <chrono>
#include <fstream>
#include <memory>
#include <numeric>
#include <thread>
#include <utility>
#include <vector>

#include "src/cobalt/bin/system-metrics/cpu_stats_fetcher_impl.h"
#include "src/cobalt/bin/system-metrics/metrics_registry.cb.h"
#include "src/cobalt/bin/utils/clock.h"
#include "src/cobalt/bin/utils/error_utils.h"
#include "src/lib/cobalt/cpp/metric_event_builder.h"

using cobalt::ErrorToString;
using cobalt::IntegerBuckets;
using cobalt::LinearIntegerBuckets;
using cobalt::MetricEventBuilder;
using cobalt::config::IntegerBucketConfig;
using fuchsia::metrics::HistogramBucket;
using fuchsia::metrics::MetricEvent;
using fuchsia::metrics::MetricEventLogger_Sync;
using fuchsia::ui::activity::State;
using fuchsia_system_metrics::FuchsiaLifetimeEventsMigratedMetricDimensionEvents;
using fuchsia_system_metrics::FuchsiaUpPingMigratedMetricDimensionUptime;
using fuchsia_system_metrics::FuchsiaUptimeMigratedMetricDimensionUptimeRange;
using std::chrono::steady_clock;

constexpr char kActivationFileSuffix[] = "activation";

namespace {

// Given a number of seconds, return the number of seconds before the next
// multiple of 1 hour.
std::chrono::seconds SecondsBeforeNextHour(std::chrono::seconds uptime) {
  return std::chrono::seconds(3600 - (uptime.count() % 3600));
}

}  // namespace

SystemMetricsDaemon::SystemMetricsDaemon(async_dispatcher_t* dispatcher,
                                         sys::ComponentContext* context)
    : SystemMetricsDaemon(
          dispatcher, context, nullptr,
          std::unique_ptr<cobalt::SteadyClock>(new cobalt::RealSteadyClock()),
          std::unique_ptr<cobalt::CpuStatsFetcher>(new cobalt::CpuStatsFetcherImpl()),
          std::make_unique<cobalt::ActivityListener>(
              fit::bind_member<&SystemMetricsDaemon::UpdateState>(this)),
          "data/") {
  InitializeLogger();
  // Connect activity listener to service provider.
  activity_provider_ = context->svc()->Connect<fuchsia::ui::activity::Provider>();
  activity_provider_->WatchState(activity_listener_->NewHandle(dispatcher));
}

SystemMetricsDaemon::SystemMetricsDaemon(
    async_dispatcher_t* dispatcher, sys::ComponentContext* context,
    fuchsia::metrics::MetricEventLogger_Sync* logger, std::unique_ptr<cobalt::SteadyClock> clock,
    std::unique_ptr<cobalt::CpuStatsFetcher> cpu_stats_fetcher,
    std::unique_ptr<cobalt::ActivityListener> activity_listener, std::string activation_file_prefix)
    : dispatcher_(dispatcher),
      context_(context),
      logger_(logger),
      start_time_(clock->Now()),
      clock_(std::move(clock)),
      cpu_stats_fetcher_(std::move(cpu_stats_fetcher)),
      activity_listener_(std::move(activity_listener)),
      activation_file_prefix_(std::move(activation_file_prefix)),
      inspector_(context),
      platform_metric_node_(inspector_.root().CreateChild(kInspecPlatformtNodeName)),
      // Diagram of hierarchy can be seen below:
      // root
      // - platform_metrics
      //   - cpu
      //     - max
      //     - mean
      metric_cpu_node_(platform_metric_node_.CreateChild(kCPUNodeName)),
      inspect_cpu_max_(metric_cpu_node_.CreateDoubleArray(kReadingCPUMax, kCPUArraySize)),
      inspect_cpu_mean_(metric_cpu_node_.CreateDoubleArray(kReadingCPUMean, kCPUArraySize)),
      unlogged_active_duration_(std::chrono::steady_clock::duration::zero()) {}

void SystemMetricsDaemon::StartLogging() {
  TRACE_DURATION("system_metrics", "SystemMetricsDaemon::StartLogging");
  // We keep gathering metrics until this process is terminated.
  RepeatedlyLogActiveTime();
  RepeatedlyLogUpPing();
  RepeatedlyLogUptime();
  RepeatedlyLogCpuUsage();
  LogLifetimeEvents();
}

void SystemMetricsDaemon::RepeatedlyLogUpPing() {
  std::chrono::seconds uptime = GetUpTime();
  std::chrono::seconds seconds_to_sleep = LogFuchsiaUpPing(uptime);
  async::PostDelayedTask(
      dispatcher_, [this]() { RepeatedlyLogUpPing(); }, zx::sec(seconds_to_sleep.count() + 5));
  return;
}

void SystemMetricsDaemon::LogLifetimeEvents() {
  LogLifetimeEventBoot();
  LogLifetimeEventActivation();
}

void SystemMetricsDaemon::LogLifetimeEventBoot() {
  if (!LogFuchsiaLifetimeEventBoot()) {
    // Pause for 5 minutes and try again.
    async::PostDelayedTask(
        dispatcher_, [this]() { LogLifetimeEventBoot(); }, zx::sec(300));
  }
}

void SystemMetricsDaemon::LogLifetimeEventActivation() {
  if (!LogFuchsiaLifetimeEventActivation()) {
    // Pause for 5 minutes and try again.
    async::PostDelayedTask(
        dispatcher_, [this]() { LogLifetimeEventActivation(); }, zx::sec(300));
  }
}

void SystemMetricsDaemon::RepeatedlyLogUptime() {
  std::chrono::seconds seconds_to_sleep = LogFuchsiaUptime();
  async::PostDelayedTask(
      dispatcher_, [this]() { RepeatedlyLogUptime(); }, zx::sec(seconds_to_sleep.count()));
}

void SystemMetricsDaemon::RepeatedlyLogCpuUsage() {
  cpu_bucket_config_ = InitializeLinearBucketConfig(
      fuchsia_system_metrics::kCpuPercentageMigratedIntBucketsFloor,
      fuchsia_system_metrics::kCpuPercentageMigratedIntBucketsNumBuckets,
      fuchsia_system_metrics::kCpuPercentageMigratedIntBucketsStepSize);
  std::chrono::seconds seconds_to_sleep = LogCpuUsage();
  async::PostDelayedTask(
      dispatcher_, [this]() { RepeatedlyLogCpuUsage(); }, zx::sec(seconds_to_sleep.count()));
}

void SystemMetricsDaemon::RepeatedlyLogActiveTime() {
  std::chrono::seconds seconds_to_sleep = LogActiveTime();
  async::PostDelayedTask(
      dispatcher_, [this]() { RepeatedlyLogActiveTime(); }, zx::sec(seconds_to_sleep.count()));
}

std::unique_ptr<IntegerBucketConfig> SystemMetricsDaemon::InitializeLinearBucketConfig(
    int64_t bucket_floor, int32_t num_buckets, int32_t step_size) {
  IntegerBuckets bucket_proto;
  LinearIntegerBuckets* linear = bucket_proto.mutable_linear();
  linear->set_floor(bucket_floor);
  linear->set_num_buckets(num_buckets);
  linear->set_step_size(step_size);
  return IntegerBucketConfig::CreateFromProto(bucket_proto);
}

std::chrono::seconds SystemMetricsDaemon::GetUpTime() {
  // Note(rudominer) We are using the startime of the SystemMetricsDaemon
  // as a proxy for the system start time. This is fine as long as we don't
  // start seeing systematic restarts of the SystemMetricsDaemon. If that
  // starts happening we should look into how to capture actual boot time.
  std::chrono::steady_clock::time_point now = clock_->Now();
  return std::chrono::duration_cast<std::chrono::seconds>(now - start_time_);
}

std::chrono::seconds SystemMetricsDaemon::LogFuchsiaUpPing(std::chrono::seconds uptime) {
  TRACE_DURATION("system_metrics", "SystemMetricsDaemon::LogFuchsiaUpPing");

  typedef FuchsiaUpPingMigratedMetricDimensionUptime Uptime;

  // We always log that we are |Up|.
  // If |uptime| is at least one minute we log that we are |UpOneMinute|.
  // If |uptime| is at least ten minutes we log that we are |UpTenMinutes|.
  // If |uptime| is at least one hour we log that we are |UpOneHour|.
  // If |uptime| is at least 12 hours we log that we are |UpTwelveHours|.
  // If |uptime| is at least 24 hours we log that we are |UpOneDay|.
  //
  // To understand the logic of this function it is important to note that
  // the events we are logging are intended to take advantage of Cobalt's
  // local aggregation feature. Thus, for example, although we log the
  // |Up| event many times throughout a calendar day, only a single
  // Observation per day will be sent from the device to the Cobalt backend
  // indicating that this device was "Up" during the day.

  if (!logger_) {
    FX_LOGS(ERROR) << "No logger present. Reconnecting...";
    InitializeLogger();
    // Something went wrong. Pause for 5 minutes.
    return std::chrono::minutes(5);
  }

  fuchsia::metrics::MetricEventLogger_LogOccurrence_Result result;
  // Always log that we are "Up".
  if (ReinitializeIfPeerClosed(logger_->LogOccurrence(
          fuchsia_system_metrics::kFuchsiaUpPingMigratedMetricId, 1, {Uptime::Up}, &result)) !=
      ZX_OK) {
    return std::chrono::minutes(5);
  }
  if (result.is_err()) {
    FX_LOGS(ERROR) << "LogOccurrence() returned error=" << ErrorToString(result.err());
  }
  if (uptime < std::chrono::minutes(1)) {
    // If we have been up for less than a minute, come back here after it
    // has been a minute.
    return std::chrono::minutes(1) - uptime;
  }
  // Log UpOneMinute
  if (ReinitializeIfPeerClosed(
          logger_->LogOccurrence(fuchsia_system_metrics::kFuchsiaUpPingMigratedMetricId, 1,
                                 {Uptime::UpOneMinute}, &result)) != ZX_OK) {
    return std::chrono::minutes(5);
  }
  if (result.is_err()) {
    FX_LOGS(ERROR) << "LogOccurrence() returned error=" << ErrorToString(result.err());
  }
  if (uptime < std::chrono::minutes(10)) {
    // If we have been up for less than 10 minutes, come back here after it
    // has been 10 minutes.
    return std::chrono::minutes(10) - uptime;
  }
  // Log UpTenMinutes
  if (ReinitializeIfPeerClosed(
          logger_->LogOccurrence(fuchsia_system_metrics::kFuchsiaUpPingMigratedMetricId, 1,
                                 {Uptime::UpTenMinutes}, &result)) != ZX_OK) {
    return std::chrono::minutes(5);
  }
  if (result.is_err()) {
    FX_LOGS(ERROR) << "LogOccurrence() returned error=" << ErrorToString(result.err());
  }
  if (uptime < std::chrono::hours(1)) {
    // If we have been up for less than an hour, come back here after it has
    // has been an hour.
    return std::chrono::hours(1) - uptime;
  }
  // Log UpOneHour
  if (ReinitializeIfPeerClosed(
          logger_->LogOccurrence(fuchsia_system_metrics::kFuchsiaUpPingMigratedMetricId, 1,
                                 {Uptime::UpOneHour}, &result)) != ZX_OK) {
    return std::chrono::minutes(5);
  }
  if (result.is_err()) {
    FX_LOGS(ERROR) << "LogOccurrence() returned error=" << ErrorToString(result.err());
  }
  if (uptime < std::chrono::hours(12)) {
    // If we have been up for less than 12 hours, come back here after *one*
    // hour. Notice this time we don't wait 12 hours to come back. The reason
    // is that it may be close to the end of the day. When the new day starts
    // we want to come back in a reasonable amount of time (we consider
    // one hour to be reasonable) so that we can log the earlier events
    // in the new day.
    return std::chrono::hours(1);
  }
  // Log UpTwelveHours.
  if (ReinitializeIfPeerClosed(
          logger_->LogOccurrence(fuchsia_system_metrics::kFuchsiaUpPingMigratedMetricId, 1,
                                 {Uptime::UpTwelveHours}, &result)) != ZX_OK) {
    return std::chrono::minutes(5);
  }
  if (result.is_err()) {
    FX_LOGS(ERROR) << "LogOccurrence() returned error=" << ErrorToString(result.err());
  }
  if (uptime < std::chrono::hours(24)) {
    // As above, come back in one hour.
    return std::chrono::hours(1);
  }
  // Log UpOneDay.
  if (ReinitializeIfPeerClosed(
          logger_->LogOccurrence(fuchsia_system_metrics::kFuchsiaUpPingMigratedMetricId, 1,
                                 {Uptime::UpOneDay}, &result)) != ZX_OK) {
    return std::chrono::minutes(5);
  }
  if (result.is_err()) {
    FX_LOGS(ERROR) << "LogOccurrence() returned error=" << ErrorToString(result.err());
  }
  if (uptime < std::chrono::hours(72)) {
    return std::chrono::hours(1);
  }
  // Log UpThreeDays.
  if (ReinitializeIfPeerClosed(
          logger_->LogOccurrence(fuchsia_system_metrics::kFuchsiaUpPingMigratedMetricId, 1,
                                 {Uptime::UpThreeDays}, &result)) != ZX_OK) {
    return std::chrono::minutes(5);
  }
  if (result.is_err()) {
    FX_LOGS(ERROR) << "LogOccurrence() returned error=" << ErrorToString(result.err());
  }
  if (uptime < std::chrono::hours(144)) {
    return std::chrono::hours(1);
  }
  // Log UpSixDays.
  if (ReinitializeIfPeerClosed(
          logger_->LogOccurrence(fuchsia_system_metrics::kFuchsiaUpPingMigratedMetricId, 1,
                                 {Uptime::UpSixDays}, &result)) != ZX_OK) {
    return std::chrono::minutes(5);
  }
  if (result.is_err()) {
    FX_LOGS(ERROR) << "LogOccurrence() returned error=" << ErrorToString(result.err());
  }
  // As above, come back in one hour.
  return std::chrono::hours(1);
}

std::chrono::seconds SystemMetricsDaemon::LogFuchsiaUptime() {
  std::chrono::seconds uptime = GetUpTime();
  if (!logger_) {
    FX_LOGS(ERROR) << "No logger present. Reconnecting...";
    InitializeLogger();
    // Something went wrong. Pause for 5 minutes.
    return std::chrono::minutes(5);
  }
  auto up_hours = std::chrono::duration_cast<std::chrono::hours>(uptime).count();
  uint32_t event_code = (up_hours < 336)
                            ? FuchsiaUptimeMigratedMetricDimensionUptimeRange::LessThanTwoWeeks
                            : event_code =
                                  FuchsiaUptimeMigratedMetricDimensionUptimeRange::TwoWeeksOrMore;

  fuchsia::metrics::MetricEventLogger_LogInteger_Result result;
  ReinitializeIfPeerClosed(logger_->LogInteger(
      fuchsia_system_metrics::kFuchsiaUptimeMigratedMetricId, up_hours, {event_code}, &result));
  if (result.is_err()) {
    FX_LOGS(ERROR) << "LogInteger() returned error=" << ErrorToString(result.err());
  }
  // Schedule a call of this function for the next multiple of an hour.
  return SecondsBeforeNextHour(uptime);
}

bool SystemMetricsDaemon::LogFuchsiaLifetimeEventBoot() {
  TRACE_DURATION("system_metrics", "SystemMetricsDaemon::LogFuchsiaLifetimeEventBoot");
  if (!logger_) {
    FX_LOGS(ERROR) << "No logger present. Reconnecting...";
    InitializeLogger();
    // Something went wrong. Pause and try again.
    return false;
  }

  fuchsia::metrics::MetricEventLogger_LogOccurrence_Result result;
  ReinitializeIfPeerClosed(
      logger_->LogOccurrence(fuchsia_system_metrics::kFuchsiaLifetimeEventsMigratedMetricId, 1,
                             {FuchsiaLifetimeEventsMigratedMetricDimensionEvents::Boot}, &result));
  if (result.is_err()) {
    FX_LOGS(ERROR) << "LogOccurrence() returned error=" << ErrorToString(result.err());
    if (result.err() == fuchsia::metrics::Error::BUFFER_FULL) {
      // Temporary error. Pause and try again.
      return false;
    }
  }
  return true;
}

bool SystemMetricsDaemon::LogFuchsiaLifetimeEventActivation() {
  TRACE_DURATION("system_metrics", "SystemMetricsDaemon::LogFuchsiaLifetimeEventActivation");
  if (!logger_) {
    FX_LOGS(ERROR) << "No logger present. Reconnecting...";
    InitializeLogger();
    // Something went wrong. Pause and try again.
    return false;
  }

  std::ifstream file(activation_file_prefix_ + kActivationFileSuffix);
  if (file) {
    // This device has already logged activation. No work required.
    return true;
  }

  fuchsia::metrics::MetricEventLogger_LogOccurrence_Result result;
  ReinitializeIfPeerClosed(logger_->LogOccurrence(
      fuchsia_system_metrics::kFuchsiaLifetimeEventsMigratedMetricId, 1,
      {FuchsiaLifetimeEventsMigratedMetricDimensionEvents::Activation}, &result));
  if (result.is_err()) {
    FX_LOGS(ERROR) << "LogOccurrence() returned error=" << ErrorToString(result.err());
    return false;
  }

  // Create a new empty file to prevent relogging activation.
  std::ofstream c(activation_file_prefix_ + kActivationFileSuffix);
  c.close();
  return true;
}

std::chrono::seconds SystemMetricsDaemon::LogCpuUsage() {
  TRACE_DURATION("system_metrics", "SystemMetricsDaemon::LogCpuUsage");
  if (!logger_) {
    FX_LOGS(ERROR) << "No logger present. Reconnecting...";
    InitializeLogger();
    return std::chrono::minutes(1);
  }
  double cpu_percentage;
  switch (cpu_stats_fetcher_->FetchCpuPercentage(&cpu_percentage)) {
    case cobalt::FetchCpuResult::Ok:
      StoreCpuData(cpu_percentage);
      return std::chrono::seconds(1);
    case cobalt::FetchCpuResult::FirstDataPoint:
      return std::chrono::seconds(1);
    case cobalt::FetchCpuResult::Error:
      return std::chrono::minutes(1);
  }
}

std::chrono::seconds SystemMetricsDaemon::LogActiveTime() {
  TRACE_DURATION("system_metrics", "SystemMetricsDaemon::LogActiveTime");
  std::lock_guard<std::mutex> lock(active_time_mutex_);

  if (!logger_) {
    FX_LOGS(ERROR) << "No logger present. Reconnecting...";
    InitializeLogger();
    return std::chrono::minutes(1);
  }
  std::chrono::steady_clock::time_point now = clock_->Now();
  if (current_state_ == fuchsia::ui::activity::State::ACTIVE) {
    unlogged_active_duration_ += (now - active_start_time_);
    active_start_time_ = now;
  }

  // Log even if no active time has elapsed, so we will get an accurate count of devices with
  // no activity during the day.
  fuchsia::metrics::MetricEventLogger_LogInteger_Result result;
  ReinitializeIfPeerClosed(logger_->LogInteger(
      fuchsia_system_metrics::kActiveTimeMetricId,
      std::chrono::duration_cast<std::chrono::seconds>(unlogged_active_duration_).count(), {},
      &result));
  if (result.is_err()) {
    FX_LOGS(ERROR) << "LogInteger() returned error=" << ErrorToString(result.err());
  } else {
    unlogged_active_duration_ = std::chrono::steady_clock::duration::zero();
  }

  return std::chrono::minutes(15);
}

void SystemMetricsDaemon::StoreCpuData(double cpu_percentage) {
  uint32_t bucket_index = cpu_bucket_config_->BucketIndex(cpu_percentage * 100);
  activity_state_to_cpu_map_[current_state_][bucket_index]++;
  cpu_data_stored_++;
  if (cpu_data_stored_ >= 10 * 60) {  // Flush every 10 minutes.
    if (LogCpuToCobalt()) {
      // If failed, attempt to log agin next time.
      activity_state_to_cpu_map_.clear();
      cpu_data_stored_ = 0;
    }
  }

  cpu_usage_accumulator_ += cpu_percentage;
  if (cpu_percentage > cpu_usage_max_) {
    cpu_usage_max_ = cpu_percentage;
  }
  // Every 10 (kInspectSamplePeriod) seconds, write to inspect
  const size_t kInspectSamplePeriod = 10;
  if (cpu_data_stored_ % kInspectSamplePeriod == 0) {
    inspect_cpu_max_.Set(cpu_array_index_, cpu_usage_max_);
    inspect_cpu_mean_.Set(cpu_array_index_++, cpu_usage_accumulator_ / kInspectSamplePeriod);
    cpu_usage_max_ = cpu_usage_accumulator_ = 0;
    if (cpu_array_index_ == kCPUArraySize) {
      cpu_array_index_ = 0;
    }
  }
}

bool SystemMetricsDaemon::LogCpuToCobalt() {
  TRACE_DURATION("system_metrics", "SystemMetricsDaemon::LogCpuToCobalt");
  using EventCode = fuchsia_system_metrics::CpuPercentageMigratedMetricDimensionDeviceState;
  std::vector<MetricEvent> events;
  auto builder = MetricEventBuilder(fuchsia_system_metrics::kCpuPercentageMigratedMetricId);
  for (const auto& pair : activity_state_to_cpu_map_) {
    std::vector<HistogramBucket> cpu_buckets_;
    for (const auto& bucket_pair : pair.second) {
      HistogramBucket bucket;
      bucket.index = bucket_pair.first;
      bucket.count = bucket_pair.second;
      cpu_buckets_.push_back(std::move(bucket));
    }
    events.push_back(builder.Clone()
                         .with_event_code(GetCobaltEventCodeForDeviceState<EventCode>(pair.first))
                         .as_integer_histogram(cpu_buckets_));
  }
  // call cobalt FIDL
  fuchsia::metrics::MetricEventLogger_LogMetricEvents_Result result;
  ReinitializeIfPeerClosed(logger_->LogMetricEvents(std::move(events), &result));
  if (result.is_err()) {
    FX_LOGS(ERROR) << "LogCpuToCobalt returned error=" << ErrorToString(result.err());
    return false;
  }
  return true;
}

void SystemMetricsDaemon::UpdateState(fuchsia::ui::activity::State state) {
  std::lock_guard<std::mutex> lock(active_time_mutex_);
  if (current_state_ == state) {
    return;
  }
  if (state == fuchsia::ui::activity::State::ACTIVE) {
    active_start_time_ = clock_->Now();
  } else {
    unlogged_active_duration_ += (clock_->Now() - active_start_time_);
    active_start_time_ = std::chrono::steady_clock::time_point();
  }
  current_state_ = state;
}

zx_status_t SystemMetricsDaemon::ReinitializeIfPeerClosed(zx_status_t zx_status) {
  if (zx_status == ZX_ERR_PEER_CLOSED) {
    FX_LOGS(ERROR) << "Logger connection closed. Reconnecting...";
    InitializeLogger();
  } else if (zx_status != ZX_OK) {
    FX_PLOGS(ERROR, zx_status) << "Logger failed";
  }
  return zx_status;
}

void SystemMetricsDaemon::InitializeLogger() {
  // Create a Cobalt Logger. The project ID is the one we specified in the
  // Cobalt metrics registry.
  // Connect to the cobalt fidl service provided by the environment.
  context_->svc()->Connect(factory_.NewRequest());
  if (!factory_) {
    FX_LOGS(ERROR) << "Unable to get LoggerFactory.";
    return;
  }

  fuchsia::metrics::MetricEventLoggerFactory_CreateMetricEventLogger_Result result;
  fuchsia::metrics::ProjectSpec project;
  project.set_customer_id(fuchsia_system_metrics::kCustomerId);
  project.set_project_id(fuchsia_system_metrics::kProjectId);
  factory_->CreateMetricEventLogger(std::move(project), logger_fidl_proxy_.NewRequest(), &result);
  if (result.is_err()) {
    FX_LOGS(ERROR) << "Unable to get Logger from factory. Error=" << ErrorToString(result.err());
    return;
  }
  logger_ = logger_fidl_proxy_.get();
  if (!logger_) {
    FX_LOGS(ERROR) << "Unable to get Logger from factory.";
  }
}
