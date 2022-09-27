// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/testapp/tests.h"

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <lib/sys/cpp/service_directory.h>

#include <ctime>

#include <rapidjson/document.h>
#include <rapidjson/pointer.h>

#include "src/cobalt/bin/testapp/internal_metrics_registry.cb.h"
#include "src/cobalt/bin/testapp/prober_metrics_registry.cb.h"
#include "src/cobalt/bin/testapp/test_constants.h"
#include "src/cobalt/bin/testapp/testapp_metrics_registry.cb.h"
#include "src/cobalt/bin/utils/base64.h"
#include "src/lib/cobalt/cpp/cobalt_event_builder.h"
#include "third_party/cobalt/src/lib/util/datetime_util.h"

namespace cobalt::testapp {

using fidl::VectorPtr;
using fuchsia::cobalt::Status;
using util::SystemClockInterface;
using util::TimeToDayIndex;
using LoggerMethod = cobalt_internal_registry::PerProjectLoggerCallsMadeMetricDimensionLoggerMethod;

namespace {
uint32_t CurrentDayIndex(SystemClockInterface* clock) {
  return TimeToDayIndex(std::chrono::system_clock::to_time_t(clock->now()), MetricDefinition::UTC);
}

bool SendAndCheckSuccess(const std::string& test_name, CobaltTestAppLogger* logger) {
  if (!logger->CheckForSuccessfulSend()) {
    FX_LOGS(INFO) << "CheckForSuccessfulSend() returned false";
    FX_LOGS(INFO) << test_name << ": FAIL";
    return false;
  }
  FX_LOGS(INFO) << test_name << ": PASS";
  return true;
}
}  // namespace

bool TestLogEvent(CobaltTestAppLogger* logger) {
  FX_LOGS(INFO) << "========================";
  FX_LOGS(INFO) << "TestLogEvent";
  for (uint32_t index : kErrorOccurredIndicesToUse) {
    if (!logger->LogEvent(cobalt_registry::kErrorOccurredMetricId, index)) {
      FX_LOGS(INFO) << "TestLogEvent: FAIL";
      return false;
    }
  }
  if (logger->LogEvent(cobalt_registry::kErrorOccurredMetricId, kErrorOccurredInvalidIndex)) {
    FX_LOGS(INFO) << "TestLogEvent: FAIL";
    return false;
  }

  return SendAndCheckSuccess("TestLogEvent", logger);
}

// file_system_cache_misses using EVENT_COUNT metric.
//
// For each |event_code| and each |component_name|, log one observation with
// a value of kFileSystemCacheMissesCountMax - event_code index.
bool TestLogEventCount(CobaltTestAppLogger* logger) {
  FX_LOGS(INFO) << "========================";
  FX_LOGS(INFO) << "TestLogEventCount";
  for (uint32_t index : kFileSystemCacheMissesIndices) {
    for (std::string name : kFileSystemCacheMissesComponentNames) {
      if (!logger->LogEventCount(cobalt_registry::kFileSystemCacheMissesMetricId, index, name,
                                 kFileSystemCacheMissesCountMax - index)) {
        FX_LOGS(INFO) << "LogEventCount(" << cobalt_registry::kFileSystemCacheMissesMetricId << ", "
                      << index << ", " << name << ", " << kFileSystemCacheMissesCountMax - index
                      << ")";
        FX_LOGS(INFO) << "TestLogEventCount: FAIL";
        return false;
      }
    }
  }

  return SendAndCheckSuccess("TestLogEventCount", logger);
}

// update_duration using ELAPSED_TIME metric.
//
// For each |event_code| and each |component_name|, log one observation in each
// exponential histogram bucket.
bool TestLogElapsedTime(CobaltTestAppLogger* logger) {
  FX_LOGS(INFO) << "========================";
  FX_LOGS(INFO) << "TestLogElapsedTime";
  for (uint32_t index : kUpdateDurationIndices) {
    for (std::string name : kUpdateDurationComponentNames) {
      for (int64_t value : kUpdateDurationValues) {
        if (!logger->LogElapsedTime(cobalt_registry::kUpdateDurationMetricId, index, name, value)) {
          FX_LOGS(INFO) << "LogElapsedTime(" << cobalt_registry::kUpdateDurationMetricId << ", "
                        << index << ", " << name << ", " << value << ")";
          FX_LOGS(INFO) << "TestLogElapsedTime: FAIL";
          return false;
        }
      }
    }
  }

  return SendAndCheckSuccess("TestLogElapsedTime", logger);
}

// game_frame_rate using FRAME_RATE metric.
//
// For each |event_code| and each |component_name|, log one observation in each
// exponential histogram bucket.
bool TestLogFrameRate(CobaltTestAppLogger* logger) {
  FX_LOGS(INFO) << "========================";
  FX_LOGS(INFO) << "TestLogFrameRate";
  for (uint32_t index : kGameFrameRateIndices) {
    for (std::string name : kGameFrameRateComponentNames) {
      for (float value : kGameFrameRateValues) {
        if (!logger->LogFrameRate(cobalt_registry::kGameFrameRateMetricId, index, name, value)) {
          FX_LOGS(INFO) << "LogFrameRate(" << cobalt_registry::kGameFrameRateMetricId << ", "
                        << index << ", " << name << ", " << value << ")";
          FX_LOGS(INFO) << "TestLogFrameRate: FAIL";
          return false;
        }
      }
    }
  }

  return SendAndCheckSuccess("TestLogFrameRate", logger);
}

// application_memory
//
// For each |event_code| and each |component_name|, log one observation in each
// exponential histogram bucket.
bool TestLogMemoryUsage(CobaltTestAppLogger* logger) {
  FX_LOGS(INFO) << "========================";
  FX_LOGS(INFO) << "TestLogMemoryUsage";
  for (uint32_t index : kApplicationMemoryIndices) {
    for (std::string name : kApplicationComponentNames) {
      for (int64_t value : kApplicationMemoryValues) {
        if (!logger->LogMemoryUsage(cobalt_registry::kApplicationMemoryMetricId, index, name,
                                    value)) {
          FX_LOGS(INFO) << "LogMemoryUsage(" << cobalt_registry::kApplicationMemoryMetricId << ", "
                        << index << ", " << name << ", " << value << ")";
          FX_LOGS(INFO) << "TestLogMemoryUsage: FAIL";
          return false;
        }
      }
    }
  }

  return SendAndCheckSuccess("TestLogMemoryUsage", logger);
}

// power_usage and bandwidth_usage
//
// For each |event_code| and each |component_name|, log one observation in each
// histogram bucket, using decreasing values per bucket.
bool TestLogIntHistogram(CobaltTestAppLogger* logger) {
  FX_LOGS(INFO) << "========================";
  FX_LOGS(INFO) << "TestLogIntHistogram";
  std::map<uint32_t, uint64_t> histogram;

  // Set up and send power_usage histogram.
  for (uint32_t bucket = 0; bucket < kPowerUsageBuckets; bucket++) {
    histogram[bucket] = kPowerUsageBuckets - bucket + 1;
  }
  for (uint32_t index : kPowerUsageIndices) {
    for (std::string name : kApplicationComponentNames) {
      if (!logger->LogIntHistogram(cobalt_registry::kPowerUsageMetricId, index, name, histogram)) {
        FX_LOGS(INFO) << "TestLogIntHistogram : FAIL";
        return false;
      }
    }
  }

  histogram.clear();

  // Set up and send bandwidth_usage histogram.
  for (uint32_t bucket = 0; bucket < kBandwidthUsageBuckets; bucket++) {
    histogram[bucket] = kBandwidthUsageBuckets - bucket + 1;
  }
  for (uint32_t index : kBandwidthUsageIndices) {
    for (std::string name : kApplicationComponentNames) {
      if (!logger->LogIntHistogram(cobalt_registry::kBandwidthUsageMetricId, index, name,
                                   histogram)) {
        FX_LOGS(INFO) << "TestLogIntHistogram : FAIL";
        return false;
      }
    }
  }

  return SendAndCheckSuccess("TestLogIntHistogram", logger);
}

bool TestLogCobaltEvent(CobaltTestAppLogger* logger) {
  FX_LOGS(INFO) << "========================";
  FX_LOGS(INFO) << "TestLogCobaltEvent";

  if (logger->LogCobaltEvent(
          CobaltEventBuilder(cobalt_registry::kErrorOccurredMetricId).as_event())) {
    // A LogEvent with no event codes is invalid.
    FX_LOGS(INFO) << "TestLogCobaltEvent: FAIL";
    return false;
  }

  if (logger->LogCobaltEvent(CobaltEventBuilder(cobalt_registry::kErrorOccurredMetricId)
                                 .with_event_code(0)
                                 .with_event_code(0)
                                 .as_event())) {
    // A LogEvent with more than 1 event code is invalid.
    FX_LOGS(INFO) << "TestLogCobaltEvent: FAIL";
    return false;
  }

  for (uint32_t index : kErrorOccurredIndicesToUse) {
    if (!logger->LogCobaltEvent(CobaltEventBuilder(cobalt_registry::kErrorOccurredMetricId)
                                    .with_event_code(index)
                                    .as_event())) {
      FX_LOGS(INFO) << "TestLogCobaltEvent: FAIL";
      return false;
    }
  }

  if (!SendAndCheckSuccess("TestLogCobaltEvent", logger)) {
    return false;
  }

  for (uint32_t index : kFileSystemCacheMissesIndices) {
    for (std::string name : kFileSystemCacheMissesComponentNames) {
      if (!logger->LogCobaltEvent(
              CobaltEventBuilder(cobalt_registry::kFileSystemCacheMissesMetricId)
                  .with_event_code(index)
                  .with_component(name)
                  .as_count_event(0, kFileSystemCacheMissesCountMax - index))) {
        FX_LOGS(INFO) << "TestLogCobaltEvent: FAIL";
        return false;
      }
    }
  }

  if (!SendAndCheckSuccess("TestLogCobaltEvent", logger)) {
    return false;
  }

  for (uint32_t index : kUpdateDurationIndices) {
    for (std::string name : kUpdateDurationComponentNames) {
      for (int64_t value : kUpdateDurationValues) {
        if (!logger->LogCobaltEvent(CobaltEventBuilder(cobalt_registry::kUpdateDurationMetricId)
                                        .with_event_code(index)
                                        .with_component(name)
                                        .as_elapsed_time(value))) {
          FX_LOGS(INFO) << "LogElapsedTime(" << cobalt_registry::kUpdateDurationMetricId << ", "
                        << index << ", " << name << ", " << value << ")";
          FX_LOGS(INFO) << "TestLogCobaltEvent: FAIL";
          return false;
        }
      }
    }
  }

  return SendAndCheckSuccess("TestLogCobaltEvent", logger);
}

////////////////////// Tests using local aggregation ///////////////////////

// A helper function which generates locally aggregated observations for
// |day_index| and checks that the number of generated observations is equal to
// the expected number for each locally aggregated report ID. The keys of
// |expected_num_obs| should be the elements of |kAggregatedReportIds|.
bool GenerateObsAndCheckCount(uint32_t day_index,
                              fuchsia::cobalt::ControllerSyncPtr* cobalt_controller,
                              uint32_t project_id,
                              std::map<std::pair<uint32_t, uint32_t>, uint64_t> expected_num_obs) {
  FX_LOGS(INFO) << "Generating locally aggregated observations for day index " << day_index;
  std::vector<fuchsia::cobalt::ReportSpec> aggregatedReportSpecs;
  std::vector<std::pair<uint32_t, uint32_t>> aggregatedReportIds;
  for (const auto& [ids, expected] : expected_num_obs) {
    const auto& [metric_id, report_id] = ids;
    fuchsia::cobalt::ReportSpec report_spec = std::move(fuchsia::cobalt::ReportSpec()
                                                            .set_customer_id(1)
                                                            .set_project_id(project_id)
                                                            .set_metric_id(metric_id)
                                                            .set_report_id(report_id));
    aggregatedReportSpecs.push_back(std::move(report_spec));
    aggregatedReportIds.push_back(ids);
  }
  std::vector<uint64_t> num_obs;
  (*cobalt_controller)
      ->GenerateAggregatedObservations(day_index, std::move(aggregatedReportSpecs), &num_obs);
  for (size_t i = 0; i < aggregatedReportIds.size(); i++) {
    const auto& [metric_id, report_id] = aggregatedReportIds[i];
    uint64_t expected = expected_num_obs[{metric_id, report_id}];
    uint64_t found = num_obs[i];
    if (found != expected) {
      FX_LOGS(INFO) << "Expected " << expected << " observations for report MetricID " << metric_id
                    << " ReportId " << report_id << ", found " << found;
      return false;
    }
  }
  return true;
}

bool CheckInspectData(CobaltTestAppLogger* logger, uint32_t project_id, LoggerMethod method,
                      int64_t expected_logger_calls,
                      const std::map<std::pair<uint32_t, uint32_t>, uint64_t>& expected_num_obs) {
  std::string inspect_json = logger->GetInspectJson();
  rapidjson::Document inspect;
  inspect.Parse(inspect_json);

  rapidjson::Pointer pointer(
      "/payload/root/cobalt_app/core/internal_metrics/logger_calls/per_method/method_" +
      std::to_string(method) + "/num_calls");
  rapidjson::Value& value = rapidjson::GetValueByPointerWithDefault(inspect, pointer, -1);
  if (value.GetInt64() != expected_logger_calls) {
    FX_LOGS(ERROR) << "Expected " << expected_logger_calls << " logger calls to method " << method
                   << ", found "
                   << (value.GetInt64() == -1 ? "none" : std::to_string(value.GetInt64()))
                   << "\nFull inspect data: " << inspect_json;
    return false;
  }

  pointer = rapidjson::Pointer(
      std::string(
          "/payload/root/cobalt_app/core/internal_metrics/logger_calls/per_project/fuchsia~1") +
      (project_id == cobalt_registry::kProjectId ? cobalt_registry::kProjectName
                                                 : cobalt_prober_registry::kProjectName) +
      "/num_calls");
  value = rapidjson::GetValueByPointerWithDefault(inspect, pointer, -1);
  if (value.GetInt64() != expected_logger_calls) {
    FX_LOGS(ERROR) << "Expected " << expected_logger_calls << " logger calls for project "
                   << project_id << ", found "
                   << (value.GetInt64() == -1 ? "none" : std::to_string(value.GetInt64()))
                   << "\nFull inspect data: " << inspect_json;
    return false;
  }

  for (const auto& [ids, expected] : expected_num_obs) {
    const auto& [metric_id, report_id] = ids;
    pointer = rapidjson::Pointer("/payload/root/cobalt_app/core/observations_stored/report_1-" +
                                 std::to_string(project_id) + "-" + std::to_string(metric_id) +
                                 "-" + std::to_string(report_id));
    value = rapidjson::GetValueByPointerWithDefault(inspect, pointer, -1);
    if (value.GetInt64() != static_cast<int64_t>(expected)) {
      FX_LOGS(ERROR) << "Expected " << expected
                     << " observations counted in Inspect for report MetricID " << metric_id
                     << " ReportId " << report_id << ", found "
                     << (value.GetInt64() == -1 ? "none" : std::to_string(value.GetInt64()))
                     << "\nFull inspect data: " << inspect_json;
      return false;
    }
  }

  return true;
}

bool TestLogEventWithAggregation(CobaltTestAppLogger* logger, SystemClockInterface* clock,
                                 fuchsia::cobalt::ControllerSyncPtr* cobalt_controller,
                                 const size_t backfill_days, uint32_t project_id) {
  FX_LOGS(INFO) << "========================";
  FX_LOGS(INFO) << "TestLogEventWithAggregation";
  uint32_t day_index = CurrentDayIndex(clock);
  for (uint32_t index : kFeaturesActiveIndices) {
    if (!logger->LogEvent(cobalt_registry::kFeaturesActiveMetricId, index)) {
      FX_LOGS(INFO) << "Failed to log event with index " << index << ".";
      FX_LOGS(INFO) << "TestLogEventWithAggregation : FAIL";
      return false;
    }
  }
  if (logger->LogEvent(cobalt_registry::kFeaturesActiveMetricId, kFeaturesActiveInvalidIndex)) {
    FX_LOGS(INFO) << "Failed to reject event with invalid index " << kFeaturesActiveInvalidIndex
                  << ".";
    FX_LOGS(INFO) << "TestLogEventWithAggregation : FAIL";
    return false;
  }

  if (CurrentDayIndex(clock) != day_index) {
    // TODO(fxbug.dev/52750): The date has changed mid-test. We are currently unable to
    // deal with this so we fail this test and our caller may try again.
    FX_LOGS(INFO) << "Quitting test because the date has changed mid-test.";
    return false;
  }

  // Expect to generate |kNumAggregatedObservations| for each day in the
  // backfill period, plus the current day. Expect to generate no observations
  // when GenerateObservations is called for the second time on the same day.
  std::map<std::pair<uint32_t, uint32_t>, uint64_t> expected_num_obs;
  std::map<std::pair<uint32_t, uint32_t>, uint64_t> expect_no_obs;
  for (const auto& [ids, expected] : kNumAggregatedObservations) {
    expected_num_obs[ids] = (1 + backfill_days) * expected;
    expect_no_obs[ids] = 0;
  }
  if (!GenerateObsAndCheckCount(day_index, cobalt_controller, project_id, expected_num_obs)) {
    FX_LOGS(INFO) << "TestLogEventWithAggregation : FAIL";
    return false;
  }
  if (!GenerateObsAndCheckCount(day_index, cobalt_controller, project_id, expect_no_obs)) {
    FX_LOGS(INFO) << "TestLogEventWithAggregation : FAIL";
    return false;
  }
  if (!CheckInspectData(logger, project_id, LoggerMethod::LogEvent,
                        std::size(kFeaturesActiveIndices), expected_num_obs)) {
    FX_LOGS(INFO) << "TestLogEventWithAggregation : FAIL";
    return false;
  }
  return SendAndCheckSuccess("TestLogEventWithAggregation", logger);
}

bool TestLogEventCountWithAggregation(CobaltTestAppLogger* logger, SystemClockInterface* clock,
                                      fuchsia::cobalt::ControllerSyncPtr* cobalt_controller,
                                      const size_t backfill_days, uint32_t project_id) {
  FX_LOGS(INFO) << "========================";
  FX_LOGS(INFO) << "TestLogEventCountWithAggregation";
  std::map<std::pair<uint32_t, uint32_t>, uint64_t> expected_num_obs;
  std::map<std::pair<uint32_t, uint32_t>, uint64_t> expect_no_obs;
  for (const auto& [ids, expected] : kNumAggregatedObservations) {
    expected_num_obs[ids] = (1 + backfill_days) * expected;
    expect_no_obs[ids] = 0;
  }
  uint32_t day_index = CurrentDayIndex(clock);
  for (uint32_t index : kConnectionAttemptsIndices) {
    for (std::string component : kConnectionAttemptsComponentNames) {
      if (index != 0) {
        // Log a count depending on the index.
        int64_t count = index * 5;
        if (!logger->LogEventCount(cobalt_registry::kConnectionAttemptsMetricId, index, component,
                                   count)) {
          FX_LOGS(INFO) << "Failed to log event count for index " << index << " and component "
                        << component << ".";
          FX_LOGS(INFO) << "TestLogEventCountWithAggregation : FAIL";
          return false;
        }
        expected_num_obs[{
            cobalt_registry::kConnectionAttemptsMetricId,
            cobalt_registry::kConnectionAttemptsConnectionAttemptsPerDeviceCountReportId}] +=
            kConnectionAttemptsNumWindowSizes;
        expected_num_obs[{
            cobalt_registry::kConnectionAttemptsMetricId,
            cobalt_registry::kConnectionAttemptsConnectionAttemptsPerDeviceHistogramReportId}] +=
            kConnectionAttemptsNumWindowSizes;
      }
    }
  }

  if (CurrentDayIndex(clock) != day_index) {
    // TODO(fxbug.dev/52750): The date has changed mid-test. We are currently unable to
    // deal with this so we fail this test and our caller may try again.
    FX_LOGS(INFO) << "Quitting test because the date has changed mid-test.";
    return false;
  }

  if (!GenerateObsAndCheckCount(day_index, cobalt_controller, project_id, expected_num_obs)) {
    FX_LOGS(INFO) << "TestLogEventCountWithAggregation : FAIL";
    return false;
  }
  if (!GenerateObsAndCheckCount(day_index, cobalt_controller, project_id, expect_no_obs)) {
    FX_LOGS(INFO) << "TestLogEventCountWithAggregation : FAIL";
    return false;
  }
  return SendAndCheckSuccess("TestLogEventCountWithAggregation", logger);
}

bool TestLogElapsedTimeWithAggregation(CobaltTestAppLogger* logger, SystemClockInterface* clock,
                                       fuchsia::cobalt::ControllerSyncPtr* cobalt_controller,
                                       const size_t backfill_days, uint32_t project_id) {
  FX_LOGS(INFO) << "========================";
  FX_LOGS(INFO) << "TestLogElapsedTimeWithAggregation";
  // Expect to generate |kNumAggregatedObservations| for each day in the
  // backfill period, plus the current day. Expect to generate no observations
  // when GenerateObservations is called for the second time on the same day.
  std::map<std::pair<uint32_t, uint32_t>, uint64_t> expected_num_obs;
  std::map<std::pair<uint32_t, uint32_t>, uint64_t> expect_no_obs;
  for (const auto& [ids, expected] : kNumAggregatedObservations) {
    expected_num_obs[ids] = (1 + backfill_days) * expected;
    expect_no_obs[ids] = 0;
  }

  uint32_t day_index = CurrentDayIndex(clock);
  for (uint32_t index : kStreamingTimeIndices) {
    for (std::string component : kStreamingTimeComponentNames) {
      // Log a duration depending on the index.
      if (index != 0) {
        int64_t duration = index * 100;
        if (!logger->LogElapsedTime(cobalt_registry::kStreamingTimeMetricId, index, component,
                                    duration)) {
          FX_LOGS(INFO) << "Failed to log elapsed time for index " << index << " and component "
                        << component << ".";
          FX_LOGS(INFO) << "TestLogElapsedTimeWithAggregation : FAIL";
          return false;
        }
        expected_num_obs[{cobalt_registry::kStreamingTimeMetricId,
                          cobalt_registry::kStreamingTimeStreamingTimePerDeviceTotalReportId}] +=
            kStreamingTimeNumWindowSizes;
        expected_num_obs[{
            cobalt_registry::kStreamingTimeMetricId,
            cobalt_registry::kStreamingTimeStreamingTimePerDeviceHistogramReportId}] +=
            kStreamingTimeNumWindowSizes;
      }
    }
  }

  if (CurrentDayIndex(clock) != day_index) {
    // TODO(fxbug.dev/52750): The date has changed mid-test. We are currently unable to
    // deal with this so we fail this test and our caller may try again.
    FX_LOGS(INFO) << "Quitting test because the date has changed mid-test.";
    return false;
  }

  if (!GenerateObsAndCheckCount(day_index, cobalt_controller, project_id, expected_num_obs)) {
    FX_LOGS(INFO) << "TestLogElapsedTimeWithAggregation : FAIL";
    return false;
  }
  if (!GenerateObsAndCheckCount(day_index, cobalt_controller, project_id, expect_no_obs)) {
    FX_LOGS(INFO) << "TestLogElapsedTimeWithAggregation : FAIL";
    return false;
  }
  return SendAndCheckSuccess("TestLogElapsedTimeWithAggregation", logger);
}

// INTEGER metrics for update_duration_new, streaming_time_new and application_memory_new.
bool TestLogInteger(CobaltTestAppLogger* logger, SystemClockInterface* clock,
                    fuchsia::cobalt::ControllerSyncPtr* cobalt_controller,
                    const size_t backfill_days, uint32_t project_id) {
  FX_LOGS(INFO) << "========================";
  FX_LOGS(INFO) << "TestLogInteger";

  // All data for a single report is aggregated into a single observation.
  std::map<std::pair<uint32_t, uint32_t>, uint64_t> expected_num_obs = {
      {{cobalt_registry::kUpdateDurationNewMetricId,
        cobalt_registry::kUpdateDurationNewUpdateDurationTimingStatsReportId},
       1},
      {{cobalt_registry::kStreamingTimeNewMetricId,
        cobalt_registry::kStreamingTimeNewStreamingTimePerDeviceTotalReportId},
       1},
      {{cobalt_registry::kApplicationMemoryNewMetricId,
        cobalt_registry::kApplicationMemoryNewApplicationMemoryHistogramsReportId},
       1},
      {{cobalt_registry::kApplicationMemoryNewMetricId,
        cobalt_registry::
            kApplicationMemoryNewApplicationMemoryHistogramsLinearConstantWidthReportId},
       1},
      {{cobalt_registry::kApplicationMemoryNewMetricId,
        cobalt_registry::kApplicationMemoryNewApplicationMemoryStatsReportId},
       1},
  };
  std::map<std::pair<uint32_t, uint32_t>, uint64_t> expect_no_obs = {
      {{cobalt_registry::kUpdateDurationNewMetricId,
        cobalt_registry::kUpdateDurationNewUpdateDurationTimingStatsReportId},
       0},
      {{cobalt_registry::kStreamingTimeNewMetricId,
        cobalt_registry::kStreamingTimeNewStreamingTimePerDeviceTotalReportId},
       0},
      {{cobalt_registry::kApplicationMemoryNewMetricId,
        cobalt_registry::kApplicationMemoryNewApplicationMemoryHistogramsReportId},
       0},
      {{cobalt_registry::kApplicationMemoryNewMetricId,
        cobalt_registry::
            kApplicationMemoryNewApplicationMemoryHistogramsLinearConstantWidthReportId},
       0},
      {{cobalt_registry::kApplicationMemoryNewMetricId,
        cobalt_registry::kApplicationMemoryNewApplicationMemoryStatsReportId},
       0},
  };

  uint32_t day_index = CurrentDayIndex(clock);

  // For each of the two event_codes, log one observation with an integer value.
  for (uint32_t error_name_index : kUpdateDurationNewErrorNameIndices) {
    for (uint32_t stage_index : kUpdateDurationNewStageIndices) {
      for (int64_t value : kUpdateDurationNewValues) {
        if (!logger->LogInteger(cobalt_registry::kUpdateDurationNewMetricId,
                                {error_name_index, stage_index}, value)) {
          FX_LOGS(INFO) << "LogInteger(" << cobalt_registry::kUpdateDurationNewMetricId
                        << ", { error_name: " << error_name_index
                        << ", update_duration_stage: " << stage_index << " }, " << value << ")";
          FX_LOGS(INFO) << "TestLogInteger : FAIL";
          return false;
        }
      }
    }
  }

  for (uint32_t type_index : kStreamingTimeNewTypeIndices) {
    for (uint32_t mod_name_index : kStreamingTimeNewModuleNameIndices) {
      for (int64_t duration : kStreamingTimeNewValues) {
        if (!logger->LogInteger(cobalt_registry::kStreamingTimeNewMetricId,
                                {type_index, mod_name_index}, duration)) {
          FX_LOGS(INFO) << "LogInteger(" << cobalt_registry::kStreamingTimeNewMetricId
                        << ", { type: " << type_index << ", module_name: " << mod_name_index
                        << " }, " << duration << ")";
          FX_LOGS(INFO) << "TestLogInteger: FAIL";
          return false;
        }
      }
    }
  }

  for (uint32_t memory_type_index : kApplicationMemoryNewMemoryTypeIndices) {
    for (uint32_t app_name_index : kApplicationMemoryNewApplicationNameIndices) {
      for (int64_t value : kApplicationMemoryNewValues) {
        if (!logger->LogInteger(cobalt_registry::kApplicationMemoryNewMetricId,
                                {memory_type_index, app_name_index}, value)) {
          FX_LOGS(INFO) << "LogInteger(" << cobalt_registry::kApplicationMemoryNewMetricId
                        << ", { memory_type: " << memory_type_index
                        << ", application_name: " << app_name_index << " }, " << value << ")";
          FX_LOGS(INFO) << "TestLogInteger: FAIL";
          return false;
        }
      }
    }
  }

  if (CurrentDayIndex(clock) != day_index) {
    // TODO(fxb/52750) The date has changed mid-test. We are currently unable to
    // deal with this so we fail this test and our caller may try again.
    FX_LOGS(INFO) << "Quitting test because the date has changed mid-test.";
    return false;
  }

  if (!GenerateObsAndCheckCount(day_index, cobalt_controller, project_id, expected_num_obs)) {
    FX_LOGS(INFO) << "TestLogInteger : FAIL";
    return false;
  }
  if (!GenerateObsAndCheckCount(day_index, cobalt_controller, project_id, expect_no_obs)) {
    FX_LOGS(INFO) << "TestLogInteger : FAIL";
    return false;
  }
  if (!CheckInspectData(logger, project_id, LoggerMethod::LogInteger,
                        std::size(kUpdateDurationNewErrorNameIndices) *
                                std::size(kUpdateDurationNewStageIndices) *
                                std::size(kUpdateDurationNewValues) +
                            std::size(kStreamingTimeNewTypeIndices) *
                                std::size(kStreamingTimeNewModuleNameIndices) *
                                std::size(kStreamingTimeNewValues) +
                            std::size(kApplicationMemoryNewMemoryTypeIndices) *
                                std::size(kApplicationMemoryNewApplicationNameIndices) *
                                std::size(kApplicationMemoryNewValues),
                        expected_num_obs)) {
    FX_LOGS(INFO) << "TestLogInteger : FAIL";
    return false;
  }
  return SendAndCheckSuccess("TestLogInteger", logger);
}

// OCCURRENCE metrics for features_active_new, file_system_cache_misses_new,
// connection_attempts_new, and error_occurred_new
bool TestLogOccurrence(CobaltTestAppLogger* logger, SystemClockInterface* clock,
                       fuchsia::cobalt::ControllerSyncPtr* cobalt_controller,
                       const size_t backfill_days, uint32_t project_id) {
  FX_LOGS(INFO) << "========================";
  FX_LOGS(INFO) << "TestLogOccurrence";

  // All occurrence count data for a single report is aggregated into a single observation, with the
  // exception of ErrorCountsExperiment report which uses REPORT_ALL.
  std::map<std::pair<uint32_t, uint32_t>, uint64_t> expected_num_obs = {
      {{cobalt_registry::kFeaturesActiveNewMetricId,
        cobalt_registry::kFeaturesActiveNewFeaturesActiveUniqueDevicesReportId},
       1},
      {{cobalt_registry::kFileSystemCacheMissesNewMetricId,
        cobalt_registry::kFileSystemCacheMissesNewFileSystemCacheMissCountsReportId},
       1},
      {{cobalt_registry::kFileSystemCacheMissesNewMetricId,
        cobalt_registry::kFileSystemCacheMissesNewFileSystemCacheMissHistogramsReportId},
       1},
      {{cobalt_registry::kFileSystemCacheMissesNewMetricId,
        cobalt_registry::kFileSystemCacheMissesNewFileSystemCacheMissStatsReportId},
       1},
      {{cobalt_registry::kConnectionAttemptsNewMetricId,
        cobalt_registry::kConnectionAttemptsNewConnectionAttemptsPerDeviceCountReportId},
       1},
      {{cobalt_registry::kErrorOccurredNewMetricId,
        cobalt_registry::kErrorOccurredNewErrorCountsExperimentReportId},
       // This metric is logged with 3 distinct experiment ids, and this report uses
       // system_profile_selection: REPORT_ALL, so there will be 3 observations generated.
       3},
  };
  std::map<std::pair<uint32_t, uint32_t>, uint64_t> expect_no_obs{
      {{cobalt_registry::kFeaturesActiveNewMetricId,
        cobalt_registry::kFeaturesActiveNewFeaturesActiveUniqueDevicesReportId},
       0},
      {{cobalt_registry::kFileSystemCacheMissesNewMetricId,
        cobalt_registry::kFileSystemCacheMissesNewFileSystemCacheMissCountsReportId},
       0},
      {{cobalt_registry::kFileSystemCacheMissesNewMetricId,
        cobalt_registry::kFileSystemCacheMissesNewFileSystemCacheMissHistogramsReportId},
       0},
      {{cobalt_registry::kFileSystemCacheMissesNewMetricId,
        cobalt_registry::kFileSystemCacheMissesNewFileSystemCacheMissStatsReportId},
       0},
      {{cobalt_registry::kConnectionAttemptsNewMetricId,
        cobalt_registry::kConnectionAttemptsNewConnectionAttemptsPerDeviceCountReportId},
       0},
      {{cobalt_registry::kErrorOccurredNewMetricId,
        cobalt_registry::kErrorOccurredNewErrorCountsExperimentReportId},
       0},
  };

  uint32_t day_index = CurrentDayIndex(clock);

  for (uint32_t skill_index : kFeaturesActiveNewSkillIndices) {
    for (uint64_t count : kFeaturesActiveNewCounts) {
      if (!logger->LogOccurrence(cobalt_registry::kFeaturesActiveNewMetricId, {skill_index},
                                 count)) {
        FX_LOGS(INFO) << "LogOccurrence(" << cobalt_registry::kFeaturesActiveNewMetricId
                      << ", { skill: " << skill_index << " }, " << count << ")";
        FX_LOGS(INFO) << "TestLogOccurrence : FAIL";
        return false;
      }
    }
  }

  for (uint32_t state_index : kFileSystemCacheMissesNewEncryptionStateIndices) {
    for (uint32_t type_index : kFileSystemCacheMissesNewFileSystemTypeIndices) {
      for (int64_t count : kFileSystemCacheMissesNewCounts) {
        if (!logger->LogOccurrence(cobalt_registry::kFileSystemCacheMissesNewMetricId,
                                   {state_index, type_index}, count)) {
          FX_LOGS(INFO) << "LogOccurrence(" << cobalt_registry::kFileSystemCacheMissesNewMetricId
                        << ", { encryption_state: " << state_index
                        << ", file_system_type: " << type_index << " }, " << count << ")";
          FX_LOGS(INFO) << "TestLogOccurrence: FAIL";
          return false;
        }
      }
    }
  }

  for (uint32_t status_index : kConnectionAttemptsNewStatusIndices) {
    for (uint32_t host_index : kConnectionAttemptsNewHostNameIndices) {
      for (int64_t count : kConnectionAttemptsNewCounts) {
        if (!logger->LogOccurrence(cobalt_registry::kConnectionAttemptsNewMetricId,
                                   {status_index, host_index}, count)) {
          FX_LOGS(INFO) << "LogOccurrence(" << cobalt_registry::kConnectionAttemptsNewMetricId
                        << ", { status: " << status_index << ", host_name: " << host_index << " }, "
                        << count << ")";
          FX_LOGS(INFO) << "TestLogOccurrence: FAIL";
          return false;
        }
      }
    }
  }

  for (uint32_t index : kErrorOccurredNewIndicesToUse) {
    for (int64_t count : kErrorOccurredNewCounts) {
      ExperimentArm arm = kExperiment;
      if (index % 2 == 0) {
        arm = kControl;
      }
      if (index == 0) {
        arm = kNone;
      }
      if (!logger->LogOccurrence(cobalt_registry::kErrorOccurredNewMetricId, {index}, count, arm)) {
        FX_LOGS(INFO) << "LogOccurrence(" << cobalt_registry::kErrorOccurredNewMetricId
                      << ", { index: " << index << " }, " << count << ")";
        FX_LOGS(INFO) << "TestLogOccurrence: FAIL";
        return false;
      }
    }
  }

  if (CurrentDayIndex(clock) != day_index) {
    // TODO(fxb/52750) The date has changed mid-test. We are currently unable to
    // deal with this so we fail this test and our caller may try again.
    FX_LOGS(INFO) << "Quitting test because the date has changed mid-test.";
    return false;
  }

  if (!GenerateObsAndCheckCount(day_index, cobalt_controller, project_id, expected_num_obs)) {
    FX_LOGS(INFO) << "TestLogOccurrence : FAIL";
    return false;
  }
  if (!GenerateObsAndCheckCount(day_index, cobalt_controller, project_id, expect_no_obs)) {
    FX_LOGS(INFO) << "TestLogOccurrence : FAIL";
    return false;
  }
  if (!CheckInspectData(
          logger, project_id, LoggerMethod::LogOccurrence,
          std::size(kFeaturesActiveNewSkillIndices) * std::size(kFeaturesActiveNewCounts) +
              std::size(kFileSystemCacheMissesNewEncryptionStateIndices) *
                  std::size(kFileSystemCacheMissesNewFileSystemTypeIndices) *
                  std::size(kFileSystemCacheMissesNewCounts) +
              std::size(kConnectionAttemptsNewStatusIndices) *
                  std::size(kConnectionAttemptsNewHostNameIndices) *
                  std::size(kConnectionAttemptsNewCounts) +
              std::size(kErrorOccurredNewIndicesToUse) * std::size(kErrorOccurredNewCounts),
          expected_num_obs)) {
    FX_LOGS(INFO) << "TestLogOccurrence : FAIL";
    return false;
  }
  return SendAndCheckSuccess("TestLogOccurrence", logger);
}

// power_usage_new and bandwidth_usage_new using INTEGER_HISTOGRAM metric.
//
// For each event vector, log one observation in each histogram bucket, using
// decreasing values per bucket.
bool TestLogIntegerHistogram(CobaltTestAppLogger* logger, SystemClockInterface* clock,
                             fuchsia::cobalt::ControllerSyncPtr* cobalt_controller,
                             const size_t backfill_days, uint32_t project_id) {
  FX_LOGS(INFO) << "========================";
  FX_LOGS(INFO) << "TestLogIntegerHistogram";
  std::map<uint32_t, uint64_t> histogram;

  // All integer histogram data is aggregated into a single observation.
  std::map<std::pair<uint32_t, uint32_t>, uint64_t> expected_num_obs = {
      {{cobalt_registry::kPowerUsageNewMetricId,
        cobalt_registry::kPowerUsageNewPowerUsageHistogramsReportId},
       1},
      {{cobalt_registry::kBandwidthUsageNewMetricId,
        cobalt_registry::kBandwidthUsageNewBandwidthUsageHistogramsReportId},
       1},
  };
  std::map<std::pair<uint32_t, uint32_t>, uint64_t> expect_no_obs = {
      {{cobalt_registry::kPowerUsageNewMetricId,
        cobalt_registry::kPowerUsageNewPowerUsageHistogramsReportId},
       0},
      {{cobalt_registry::kBandwidthUsageNewMetricId,
        cobalt_registry::kBandwidthUsageNewBandwidthUsageHistogramsReportId},
       0},
  };

  uint32_t day_index = CurrentDayIndex(clock);

  // Set up and send power_usage_new histograms.
  for (uint32_t bucket = 0; bucket < kPowerUsageNewBuckets; bucket++) {
    histogram[bucket] = kPowerUsageNewBuckets - bucket + 1;
  }
  for (uint32_t state_index : kPowerUsageNewApplicationStateIndices) {
    for (uint32_t app_name_index : kPowerUsageNewApplicationNameIndices) {
      if (!logger->LogIntegerHistogram(cobalt_registry::kPowerUsageNewMetricId,
                                       {state_index, app_name_index}, histogram)) {
        FX_LOGS(INFO) << "LogIntegerHistogram(" << cobalt_registry::kPowerUsageNewMetricId
                      << ", { application_state: " << state_index
                      << ", application_name: " << app_name_index << " }, buckets["
                      << kPowerUsageNewBuckets << "])";
        FX_LOGS(INFO) << "TestLogIntegerHistogram : FAIL";
        return false;
      }
    }
  }

  histogram.clear();

  // Set up and send bandwidth_usage_new histograms.
  for (uint32_t bucket = 0; bucket < kBandwidthUsageNewBuckets; bucket++) {
    histogram[bucket] = kBandwidthUsageNewBuckets - bucket + 1;
  }
  for (uint32_t state_index : kBandwidthUsageNewApplicationStateIndices) {
    for (uint32_t app_name_index : kBandwidthUsageNewApplicationNameIndices) {
      if (!logger->LogIntegerHistogram(cobalt_registry::kBandwidthUsageNewMetricId,
                                       {state_index, app_name_index}, histogram)) {
        FX_LOGS(INFO) << "LogIntegerHistogram(" << cobalt_registry::kBandwidthUsageNewMetricId
                      << ", { application_state: " << state_index
                      << ", application_name: " << app_name_index << " }, buckets["
                      << kBandwidthUsageNewBuckets << "])";
        FX_LOGS(INFO) << "TestLogIntegerHistogram : FAIL";
        return false;
      }
    }
  }

  if (CurrentDayIndex(clock) != day_index) {
    // TODO(fxb/52750) The date has changed mid-test. We are currently unable to
    // deal with this so we fail this test and our caller may try again.
    FX_LOGS(INFO) << "Quitting test because the date has changed mid-test.";
    return false;
  }

  if (!GenerateObsAndCheckCount(day_index, cobalt_controller, project_id, expected_num_obs)) {
    FX_LOGS(INFO) << "TestLogIntegerHistogram : FAIL";
    return false;
  }
  if (!GenerateObsAndCheckCount(day_index, cobalt_controller, project_id, expect_no_obs)) {
    FX_LOGS(INFO) << "TestLogIntegerHistogram : FAIL";
    return false;
  }
  if (!CheckInspectData(logger, project_id, LoggerMethod::LogIntegerHistogram,
                        std::size(kPowerUsageNewApplicationStateIndices) *
                                std::size(kPowerUsageNewApplicationNameIndices) +
                            std::size(kBandwidthUsageNewApplicationStateIndices) *
                                std::size(kBandwidthUsageNewApplicationNameIndices),
                        expected_num_obs)) {
    FX_LOGS(INFO) << "TestLogIntegerHistogram : FAIL";
    return false;
  }
  return SendAndCheckSuccess("TestLogIntegerHistogram", logger);
}

bool CheckInspectData(uint32_t id, std::map<uint32_t, uint32_t> map, int i, size_t i_1);
// error_occurred_components using STRING metric.
//
// For each of the three event_codes, log each of the five application components.
bool TestLogString(CobaltTestAppLogger* logger, SystemClockInterface* clock,
                   fuchsia::cobalt::ControllerSyncPtr* cobalt_controller,
                   const size_t backfill_days, uint32_t project_id) {
  FX_LOGS(INFO) << "========================";
  FX_LOGS(INFO) << "TestLogString";

  // All string data is aggregated into a single observation.
  std::map<std::pair<uint32_t, uint32_t>, uint64_t> expected_num_obs;
  std::map<std::pair<uint32_t, uint32_t>, uint64_t> expect_no_obs;
  expected_num_obs[{cobalt_registry::kErrorOccurredComponentsMetricId,
                    cobalt_registry::kErrorOccurredComponentsErrorCountsReportId}] = 1;
  expect_no_obs[{cobalt_registry::kErrorOccurredComponentsMetricId,
                 cobalt_registry::kErrorOccurredComponentsErrorCountsReportId}] = 0;

  uint32_t day_index = CurrentDayIndex(clock);
  for (uint32_t status_index : kErrorOccurredComponentsStatusIndices) {
    for (std::string component : kApplicationComponentNames) {
      if (!logger->LogString(cobalt_registry::kErrorOccurredComponentsMetricId, {status_index},
                             component)) {
        FX_LOGS(INFO) << "LogString(" << cobalt_registry::kErrorOccurredComponentsMetricId
                      << ", { status: " << status_index << " }, " << component << ")";
        FX_LOGS(INFO) << "TestLogString : FAIL";
        return false;
      }
    }
  }

  if (CurrentDayIndex(clock) != day_index) {
    // TODO(fxb/52750) The date has changed mid-test. We are currently unable to
    // deal with this so we fail this test and our caller may try again.
    FX_LOGS(INFO) << "Quitting test because the date has changed mid-test.";
    return false;
  }

  if (!GenerateObsAndCheckCount(day_index, cobalt_controller, project_id, expected_num_obs)) {
    FX_LOGS(INFO) << "TestLogString : FAIL";
    return false;
  }
  if (!GenerateObsAndCheckCount(day_index, cobalt_controller, project_id, expect_no_obs)) {
    FX_LOGS(INFO) << "TestLogString : FAIL";
    return false;
  }
  if (!CheckInspectData(
          logger, project_id, LoggerMethod::LogString,
          std::size(kErrorOccurredComponentsStatusIndices) * std::size(kApplicationComponentNames),
          expected_num_obs)) {
    FX_LOGS(INFO) << "TestLogString : FAIL";
    return false;
  }
  return SendAndCheckSuccess("TestLogString", logger);
}

}  // namespace cobalt::testapp
