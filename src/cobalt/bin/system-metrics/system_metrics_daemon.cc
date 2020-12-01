// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The cobalt system metrics collection daemon uses cobalt to log system metrics
// on a regular basis.
#include "src/cobalt/bin/system-metrics/system_metrics_daemon.h"

#include <assert.h>
#include <fcntl.h>
#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/sys/inspect/cpp/component.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/zx/resource.h>
#include <sys/statvfs.h>
#include <zircon/status.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <memory>
#include <numeric>
#include <thread>
#include <utility>
#include <vector>

#include "src/cobalt/bin/system-metrics/cpu_stats_fetcher_impl.h"
#include "src/cobalt/bin/system-metrics/log_stats_fetcher_impl.h"
#include "src/cobalt/bin/system-metrics/metrics_registry.cb.h"
#include "src/cobalt/bin/utils/clock.h"
#include "src/cobalt/bin/utils/status_utils.h"
#include "src/lib/cobalt/cpp/cobalt_event_builder.h"

using cobalt::CobaltEventBuilder;
using cobalt::IntegerBuckets;
using cobalt::LinearIntegerBuckets;
using cobalt::StatusToString;
using cobalt::config::IntegerBucketConfig;
using fuchsia::cobalt::CobaltEvent;
using fuchsia::cobalt::HistogramBucket;
using fuchsia::cobalt::Logger_Sync;
using fuchsia::ui::activity::State;
using fuchsia_system_metrics::FuchsiaLifetimeEventsMetricDimensionEvents;
using fuchsia_system_metrics::FuchsiaUpPingMetricDimensionUptime;
using fuchsia_system_metrics::FuchsiaUptimeMetricDimensionUptimeRange;
using std::chrono::steady_clock;

constexpr char kActivationFileSuffix[] = "activation";

namespace {

// Given a number of seconds, return the number of seconds before the next
// multiple of 1 hour.
std::chrono::seconds SecondsBeforeNextHour(std::chrono::seconds uptime) {
  return std::chrono::seconds(3600 - (uptime.count() % 3600));
}

constexpr char kDefaultGranularErrorStatsSpecFilePath[] =
    "/config/data/granular_error_stats_specs.txt";

}  // namespace

const SystemMetricsDaemon::MetricSpecs SystemMetricsDaemon::MetricSpecs::INVALID = {0, 0, 0};

// static
SystemMetricsDaemon::MetricSpecs SystemMetricsDaemon::LoadGranularErrorStatsSpecs(
    const char* spec_file_path) {
  std::ifstream spec_file_stream(spec_file_path);
  SystemMetricsDaemon::MetricSpecs result;
  if (!(spec_file_stream >> result.customer_id)) {
    FX_LOGS(ERROR) << "Unable to read customer ID from granular error stats spec file";
    return SystemMetricsDaemon::MetricSpecs::INVALID;
  }
  if (!(spec_file_stream >> result.project_id)) {
    FX_LOGS(ERROR) << "Unable to read project ID from granular error stats spec file";
    return SystemMetricsDaemon::MetricSpecs::INVALID;
  }
  if (!(spec_file_stream >> result.metric_id)) {
    FX_LOGS(ERROR) << "Unable to read metric ID from granular error stats spec file";
    return SystemMetricsDaemon::MetricSpecs::INVALID;
  }
  return result;
}

SystemMetricsDaemon::SystemMetricsDaemon(async_dispatcher_t* dispatcher,
                                         sys::ComponentContext* context)
    : SystemMetricsDaemon(
          dispatcher, context, LoadGranularErrorStatsSpecs(kDefaultGranularErrorStatsSpecFilePath),
          nullptr, nullptr, std::unique_ptr<cobalt::SteadyClock>(new cobalt::RealSteadyClock()),
          std::unique_ptr<cobalt::CpuStatsFetcher>(new cobalt::CpuStatsFetcherImpl()),
          std::unique_ptr<cobalt::LogStatsFetcher>(
              new cobalt::LogStatsFetcherImpl(dispatcher, context)),
          std::make_unique<cobalt::ActivityListener>(
              fit::bind_member(this, &SystemMetricsDaemon::UpdateState)),
          "data/") {
  InitializeLogger();
  InitializeGranularErrorStatsLogger();
  // Connect activity listener to service provider.
  activity_provider_ = context->svc()->Connect<fuchsia::ui::activity::Provider>();
  activity_provider_->WatchState(activity_listener_->NewHandle(dispatcher));
}

SystemMetricsDaemon::SystemMetricsDaemon(
    async_dispatcher_t* dispatcher, sys::ComponentContext* context,
    const MetricSpecs& granular_error_stats_specs, fuchsia::cobalt::Logger_Sync* logger,
    fuchsia::cobalt::Logger_Sync* granular_error_stats_logger,
    std::unique_ptr<cobalt::SteadyClock> clock,
    std::unique_ptr<cobalt::CpuStatsFetcher> cpu_stats_fetcher,
    std::unique_ptr<cobalt::LogStatsFetcher> log_stats_fetcher,
    std::unique_ptr<cobalt::ActivityListener> activity_listener, std::string activation_file_prefix)
    : dispatcher_(dispatcher),
      context_(context),
      granular_error_stats_specs_(granular_error_stats_specs),
      logger_(logger),
      granular_error_stats_logger_(granular_error_stats_logger),
      start_time_(clock->Now()),
      clock_(std::move(clock)),
      cpu_stats_fetcher_(std::move(cpu_stats_fetcher)),
      log_stats_fetcher_(std::move(log_stats_fetcher)),
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
      inspect_cpu_mean_(metric_cpu_node_.CreateDoubleArray(kReadingCPUMean, kCPUArraySize)) {}

void SystemMetricsDaemon::StartLogging() {
  TRACE_DURATION("system_metrics", "SystemMetricsDaemon::StartLogging");
  // We keep gathering metrics until this process is terminated.
  RepeatedlyLogUpPing();
  RepeatedlyLogUptime();
  RepeatedlyLogCpuUsage();
  RepeatedlyLogLogStats();
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
  return;
}

void SystemMetricsDaemon::RepeatedlyLogCpuUsage() {
  cpu_bucket_config_ =
      InitializeLinearBucketConfig(fuchsia_system_metrics::kCpuPercentageIntBucketsFloor,
                                   fuchsia_system_metrics::kCpuPercentageIntBucketsNumBuckets,
                                   fuchsia_system_metrics::kCpuPercentageIntBucketsStepSize);
  std::chrono::seconds seconds_to_sleep = LogCpuUsage();
  async::PostDelayedTask(
      dispatcher_, [this]() { RepeatedlyLogCpuUsage(); }, zx::sec(seconds_to_sleep.count()));
  return;
}

void SystemMetricsDaemon::RepeatedlyLogLogStats() {
  // Repeatedly run LogLogStats every 15 minutes.
  async::PostDelayedTask(
      dispatcher_,
      [this]() {
        LogLogStats();
        RepeatedlyLogLogStats();
      },
      zx::sec(900));
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
  auto now = clock_->Now();
  return std::chrono::duration_cast<std::chrono::seconds>(now - start_time_);
}

std::chrono::seconds SystemMetricsDaemon::LogFuchsiaUpPing(std::chrono::seconds uptime) {
  TRACE_DURATION("system_metrics", "SystemMetricsDaemon::LogFuchsiaUpPing");

  typedef FuchsiaUpPingMetricDimensionUptime Uptime;

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

  fuchsia::cobalt::Status status = fuchsia::cobalt::Status::INTERNAL_ERROR;
  // Always log that we are "Up".
  if (ReinitializeIfPeerClosed(logger_->LogEvent(fuchsia_system_metrics::kFuchsiaUpPingMetricId,
                                                 Uptime::Up, &status)) != ZX_OK) {
    return std::chrono::minutes(5);
  }
  if (status != fuchsia::cobalt::Status::OK) {
    FX_LOGS(ERROR) << "LogEvent() returned status=" << StatusToString(status);
  }
  if (uptime < std::chrono::minutes(1)) {
    // If we have been up for less than a minute, come back here after it
    // has been a minute.
    return std::chrono::minutes(1) - uptime;
  }
  // Log UpOneMinute
  status = fuchsia::cobalt::Status::INTERNAL_ERROR;
  if (ReinitializeIfPeerClosed(logger_->LogEvent(fuchsia_system_metrics::kFuchsiaUpPingMetricId,
                                                 Uptime::UpOneMinute, &status)) != ZX_OK) {
    return std::chrono::minutes(5);
  }
  if (status != fuchsia::cobalt::Status::OK) {
    FX_LOGS(ERROR) << "LogEvent() returned status=" << StatusToString(status);
  }
  if (uptime < std::chrono::minutes(10)) {
    // If we have been up for less than 10 minutes, come back here after it
    // has been 10 minutes.
    return std::chrono::minutes(10) - uptime;
  }
  // Log UpTenMinutes
  status = fuchsia::cobalt::Status::INTERNAL_ERROR;
  if (ReinitializeIfPeerClosed(logger_->LogEvent(fuchsia_system_metrics::kFuchsiaUpPingMetricId,
                                                 Uptime::UpTenMinutes, &status)) != ZX_OK) {
    return std::chrono::minutes(5);
  }
  if (status != fuchsia::cobalt::Status::OK) {
    FX_LOGS(ERROR) << "LogEvent() returned status=" << StatusToString(status);
  }
  if (uptime < std::chrono::hours(1)) {
    // If we have been up for less than an hour, come back here after it has
    // has been an hour.
    return std::chrono::hours(1) - uptime;
  }
  // Log UpOneHour
  status = fuchsia::cobalt::Status::INTERNAL_ERROR;
  if (ReinitializeIfPeerClosed(logger_->LogEvent(fuchsia_system_metrics::kFuchsiaUpPingMetricId,
                                                 Uptime::UpOneHour, &status)) != ZX_OK) {
    return std::chrono::minutes(5);
  }
  if (status != fuchsia::cobalt::Status::OK) {
    FX_LOGS(ERROR) << "LogEvent() returned status=" << StatusToString(status);
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
  status = fuchsia::cobalt::Status::INTERNAL_ERROR;
  if (ReinitializeIfPeerClosed(logger_->LogEvent(fuchsia_system_metrics::kFuchsiaUpPingMetricId,
                                                 Uptime::UpTwelveHours, &status)) != ZX_OK) {
    return std::chrono::minutes(5);
  }
  if (status != fuchsia::cobalt::Status::OK) {
    FX_LOGS(ERROR) << "LogEvent() returned status=" << StatusToString(status);
  }
  if (uptime < std::chrono::hours(24)) {
    // As above, come back in one hour.
    return std::chrono::hours(1);
  }
  // Log UpOneDay.
  status = fuchsia::cobalt::Status::INTERNAL_ERROR;
  if (ReinitializeIfPeerClosed(logger_->LogEvent(fuchsia_system_metrics::kFuchsiaUpPingMetricId,
                                                 Uptime::UpOneDay, &status)) != ZX_OK) {
    return std::chrono::minutes(5);
  }
  if (status != fuchsia::cobalt::Status::OK) {
    FX_LOGS(ERROR) << "LogEvent() returned status=" << StatusToString(status);
  }
  if (uptime < std::chrono::hours(72)) {
    return std::chrono::hours(1);
  }
  // Log UpThreeDays.
  status = fuchsia::cobalt::Status::INTERNAL_ERROR;
  if (ReinitializeIfPeerClosed(logger_->LogEvent(fuchsia_system_metrics::kFuchsiaUpPingMetricId,
                                                 Uptime::UpThreeDays, &status)) != ZX_OK) {
    return std::chrono::minutes(5);
  }
  if (status != fuchsia::cobalt::Status::OK) {
    FX_LOGS(ERROR) << "LogEvent() returned status=" << StatusToString(status);
  }
  if (uptime < std::chrono::hours(144)) {
    return std::chrono::hours(1);
  }
  // Log UpSixDays.
  status = fuchsia::cobalt::Status::INTERNAL_ERROR;
  if (ReinitializeIfPeerClosed(logger_->LogEvent(fuchsia_system_metrics::kFuchsiaUpPingMetricId,
                                                 Uptime::UpSixDays, &status)) != ZX_OK) {
    return std::chrono::minutes(5);
  }
  if (status != fuchsia::cobalt::Status::OK) {
    FX_LOGS(ERROR) << "LogEvent() returned status=" << StatusToString(status);
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
                            ? FuchsiaUptimeMetricDimensionUptimeRange::LessThanTwoWeeks
                            : event_code = FuchsiaUptimeMetricDimensionUptimeRange::TwoWeeksOrMore;

  fuchsia::cobalt::Status status;
  ReinitializeIfPeerClosed(logger_->LogElapsedTime(fuchsia_system_metrics::kFuchsiaUptimeMetricId,
                                                   event_code, "", up_hours, &status));
  if (status != fuchsia::cobalt::Status::OK) {
    FX_LOGS(ERROR) << "LogCobaltEvent() returned status=" << StatusToString(status);
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

  fuchsia::cobalt::Status status = fuchsia::cobalt::Status::INTERNAL_ERROR;
  ReinitializeIfPeerClosed(logger_->LogEvent(fuchsia_system_metrics::kFuchsiaLifetimeEventsMetricId,
                                             FuchsiaLifetimeEventsMetricDimensionEvents::Boot,
                                             &status));
  if (status != fuchsia::cobalt::Status::OK) {
    FX_LOGS(ERROR) << "LogEvent() returned status=" << StatusToString(status);
    if (status == fuchsia::cobalt::Status::BUFFER_FULL) {
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

  fuchsia::cobalt::Status status = fuchsia::cobalt::Status::INTERNAL_ERROR;
  ReinitializeIfPeerClosed(logger_->LogEvent(fuchsia_system_metrics::kFuchsiaLifetimeEventsMetricId,
                                             FuchsiaLifetimeEventsMetricDimensionEvents::Activation,
                                             &status));
  if (status != fuchsia::cobalt::Status::OK) {
    FX_LOGS(ERROR) << "LogEvent() returned status=" << StatusToString(status);
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

void SystemMetricsDaemon::LogLogStats() {
  TRACE_DURATION("system_metrics", "SystemMetricsDaemon::LogLogStats");
  if (!logger_) {
    FX_LOGS(ERROR) << "No logger present. Reconnecting...";
    InitializeLogger();
    return;
  }
  log_stats_fetcher_->FetchMetrics([this](const cobalt::LogStatsFetcher::Metrics& metrics) {
    std::vector<CobaltEvent> events;

    events.push_back(CobaltEventBuilder(fuchsia_system_metrics::kErrorLogCountMetricId)
                         .as_count_event(0, metrics.error_count));

    events.push_back(CobaltEventBuilder(fuchsia_system_metrics::kKernelLogCountMetricId)
                         .as_count_event(0, metrics.klog_count));

    for (auto& it : metrics.per_component_error_count) {
      events.push_back(
          CobaltEventBuilder(fuchsia_system_metrics::kPerComponentErrorLogCountMetricId)
              .with_event_code(it.first)
              .as_count_event(0, it.second));
    }

    fuchsia::cobalt::Status status = fuchsia::cobalt::Status::INTERNAL_ERROR;
    ReinitializeIfPeerClosed(logger_->LogCobaltEvents(std::move(events), &status));
    if (status != fuchsia::cobalt::Status::OK) {
      FX_LOGS(ERROR) << "LogCobaltEvents() for error log stats returned status="
                     << StatusToString(status);
    }

    std::vector<CobaltEvent> granular_events;
    for (auto& record : metrics.granular_stats) {
      // Component can only be up to 64 bytes, so truncate the beginning of the file path if it
      // doesn't fit.
      std::string component = record.file_path.size() > 64
                                  ? record.file_path.substr(record.file_path.size() - 64)
                                  : record.file_path;
      granular_events.push_back(CobaltEventBuilder(granular_error_stats_specs_.metric_id)
                                    .with_event_code((record.line_no - 1) % 1023)
                                    .with_component(component)
                                    .as_count_event(0, record.count));
    }
    if (!granular_events.empty()) {
      status = fuchsia::cobalt::Status::INTERNAL_ERROR;
      ReinitializeGranularErrorStatsIfPeerClosed(
          granular_error_stats_logger_->LogCobaltEvents(std::move(granular_events), &status));
      if (status != fuchsia::cobalt::Status::OK) {
        FX_LOGS(ERROR) << "LogCobaltEvents() for granular error log stats returned status="
                       << StatusToString(status);
      }
    }
  });
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
  using EventCode = fuchsia_system_metrics::CpuPercentageMetricDimensionDeviceState;
  std::vector<CobaltEvent> events;
  auto builder = CobaltEventBuilder(fuchsia_system_metrics::kCpuPercentageMetricId);
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
                         .as_int_histogram(cpu_buckets_));
  }
  // call cobalt FIDL
  fuchsia::cobalt::Status status = fuchsia::cobalt::Status::INTERNAL_ERROR;
  ReinitializeIfPeerClosed(logger_->LogCobaltEvents(std::move(events), &status));
  if (status != fuchsia::cobalt::Status::OK) {
    FX_LOGS(ERROR) << "LogCpuToCobalt returned status=" << StatusToString(status);
    return false;
  }
  return true;
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
  fuchsia::cobalt::Status status = fuchsia::cobalt::Status::INTERNAL_ERROR;
  // Create a Cobalt Logger. The project ID is the one we specified in the
  // Cobalt metrics registry.
  // Connect to the cobalt fidl service provided by the environment.
  context_->svc()->Connect(factory_.NewRequest());
  if (!factory_) {
    FX_LOGS(ERROR) << "Unable to get LoggerFactory.";
    return;
  }

  factory_->CreateLoggerFromProjectId(fuchsia_system_metrics::kProjectId,
                                      logger_fidl_proxy_.NewRequest(), &status);
  if (status != fuchsia::cobalt::Status::OK) {
    FX_LOGS(ERROR) << "Unable to get Logger from factory. Status=" << StatusToString(status);
    return;
  }
  logger_ = logger_fidl_proxy_.get();
  if (!logger_) {
    FX_LOGS(ERROR) << "Unable to get Logger from factory.";
  }
}

zx_status_t SystemMetricsDaemon::ReinitializeGranularErrorStatsIfPeerClosed(zx_status_t zx_status) {
  if (zx_status == ZX_ERR_PEER_CLOSED) {
    FX_LOGS(ERROR) << "Granular error stats logger connection closed. Reconnecting...";
    InitializeGranularErrorStatsLogger();
  }
  return zx_status;
}

void SystemMetricsDaemon::InitializeGranularErrorStatsLogger() {
  fuchsia::cobalt::Status status = fuchsia::cobalt::Status::INTERNAL_ERROR;
  context_->svc()->Connect(factory_.NewRequest());
  if (!factory_) {
    FX_LOGS(ERROR) << "Unable to get LoggerFactory.";
    return;
  }

  factory_->CreateLoggerFromProjectSpec(
      granular_error_stats_specs_.customer_id, granular_error_stats_specs_.project_id,
      granular_error_stats_logger_fidl_proxy_.NewRequest(), &status);

  if (status != fuchsia::cobalt::Status::OK) {
    FX_LOGS(ERROR) << "Unable to get granular error stats Logger from factory. Status="
                   << StatusToString(status);
    return;
  }
  granular_error_stats_logger_ = granular_error_stats_logger_fidl_proxy_.get();
  if (!granular_error_stats_logger_) {
    FX_LOGS(ERROR) << "Unable to get granular error stats Logger from factory.";
  }
}
