// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/testapp/tests.h"

#include "src/cobalt/bin/testapp/prober_metrics_registry.cb.h"
#include "src/cobalt/bin/testapp/test_constants.h"
#include "src/cobalt/bin/testapp/testapp_metrics_registry.cb.h"
#include "src/cobalt/bin/utils/base64.h"
#include "src/lib/cobalt/cpp/cobalt_event_builder.h"
#include "third_party/cobalt/src/lib/util/datetime_util.h"

namespace cobalt {

using util::SystemClockInterface;
using util::TimeToDayIndex;

namespace testapp {

using fidl::VectorPtr;
using fuchsia::cobalt::Status;

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

bool TestLogCustomEvent(CobaltTestAppLogger* logger) {
  FX_LOGS(INFO) << "========================";
  FX_LOGS(INFO) << "TestLogCustomEvent";
  bool success =
      logger->LogCustomMetricsTestProto(cobalt_registry::kQueryResponseMetricId, "test", 100, 1);

  FX_LOGS(INFO) << "TestLogCustomEvent : " << (success ? "PASS" : "FAIL");

  return SendAndCheckSuccess("TestLogCustomEvent", logger);
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
                              std::map<uint32_t, uint64_t> expected_num_obs) {
  FX_LOGS(INFO) << "Generating locally aggregated observations for day index " << day_index;
  std::vector<uint64_t> num_obs;
  (*cobalt_controller)->GenerateAggregatedObservations(day_index, kAggregatedReportIds, &num_obs);
  for (size_t i = 0; i < kAggregatedReportIds.size(); i++) {
    auto report_id = kAggregatedReportIds[i];
    auto expected = expected_num_obs[report_id];
    auto found = num_obs[i];
    if (found != expected) {
      FX_LOGS(INFO) << "Expected " << expected << " observations for report ID " << report_id
                    << ", found " << found;
      return false;
    }
  }
  return true;
}

bool TestLogEventWithAggregation(CobaltTestAppLogger* logger, SystemClockInterface* clock,
                                 fuchsia::cobalt::ControllerSyncPtr* cobalt_controller,
                                 const size_t backfill_days) {
  FX_LOGS(INFO) << "========================";
  FX_LOGS(INFO) << "TestLogEventWithAggregation";
  auto day_index = CurrentDayIndex(clock);
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
  std::map<uint32_t, uint64_t> expected_num_obs;
  std::map<uint32_t, uint64_t> expect_no_obs;
  for (const auto& pair : kNumAggregatedObservations) {
    expected_num_obs[pair.first] = (1 + backfill_days) * pair.second;
    expect_no_obs[pair.first] = 0;
  }
  if (!GenerateObsAndCheckCount(day_index, cobalt_controller, expected_num_obs)) {
    FX_LOGS(INFO) << "TestLogEventWithAggregation : FAIL";
    return false;
  }
  if (!GenerateObsAndCheckCount(day_index, cobalt_controller, expect_no_obs)) {
    FX_LOGS(INFO) << "TestLogEventWithAggregation : FAIL";
    return false;
  }
  return SendAndCheckSuccess("TestLogEventWithAggregation", logger);
}

bool TestLogEventCountWithAggregation(CobaltTestAppLogger* logger, SystemClockInterface* clock,
                                      fuchsia::cobalt::ControllerSyncPtr* cobalt_controller,
                                      const size_t backfill_days) {
  FX_LOGS(INFO) << "========================";
  FX_LOGS(INFO) << "TestLogEventCountWithAggregation";
  std::map<uint32_t, uint64_t> expected_num_obs;
  std::map<uint32_t, uint64_t> expect_no_obs;
  for (const auto& pair : kNumAggregatedObservations) {
    expected_num_obs[pair.first] = (1 + backfill_days) * pair.second;
    expect_no_obs[pair.first] = 0;
  }
  auto day_index = CurrentDayIndex(clock);
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
        expected_num_obs
            [cobalt_registry::kConnectionAttemptsConnectionAttemptsPerDeviceCountReportId] +=
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

  if (!GenerateObsAndCheckCount(day_index, cobalt_controller, expected_num_obs)) {
    FX_LOGS(INFO) << "TestLogEventCountWithAggregation : FAIL";
    return false;
  }
  if (!GenerateObsAndCheckCount(day_index, cobalt_controller, expect_no_obs)) {
    FX_LOGS(INFO) << "TestLogEventCountWithAggregation : FAIL";
    return false;
  }
  return SendAndCheckSuccess("TestLogEventCountWithAggregation", logger);
}

bool TestLogElapsedTimeWithAggregation(CobaltTestAppLogger* logger, SystemClockInterface* clock,
                                       fuchsia::cobalt::ControllerSyncPtr* cobalt_controller,
                                       const size_t backfill_days) {
  FX_LOGS(INFO) << "========================";
  FX_LOGS(INFO) << "TestLogElapsedTimeWithAggregation";
  // Expect to generate |kNumAggregatedObservations| for each day in the
  // backfill period, plus the current day. Expect to generate no observations
  // when GenerateObservations is called for the second time on the same day.
  std::map<uint32_t, uint64_t> expected_num_obs;
  std::map<uint32_t, uint64_t> expect_no_obs;
  for (const auto& pair : kNumAggregatedObservations) {
    expected_num_obs[pair.first] = (1 + backfill_days) * pair.second;
    expect_no_obs[pair.first] = 0;
  }

  auto day_index = CurrentDayIndex(clock);
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
        expected_num_obs[cobalt_registry::kStreamingTimeStreamingTimePerDeviceTotalReportId] +=
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

  if (!GenerateObsAndCheckCount(day_index, cobalt_controller, expected_num_obs)) {
    FX_LOGS(INFO) << "TestLogElapsedTimeWithAggregation : FAIL";
    return false;
  }
  if (!GenerateObsAndCheckCount(day_index, cobalt_controller, expect_no_obs)) {
    FX_LOGS(INFO) << "TestLogElapsedTimeWithAggregation : FAIL";
    return false;
  }
  return SendAndCheckSuccess("TestLogElapsedTimeWithAggregation", logger);
}

// update_duration_new using INTEGER metric.
//
// For each of the two event_codes, log one observation with an integer value.
bool TestLogInteger(CobaltTestAppLogger* logger, SystemClockInterface* clock,
                                      fuchsia::cobalt::ControllerSyncPtr* cobalt_controller,
                                      const size_t backfill_days) {
  FX_LOGS(INFO) << "========================";
  FX_LOGS(INFO) << "TestLogInteger";
  // Expect to generate |kNumAggregatedObservations| for each day in the
  // backfill period, plus the current day. Expect to generate no observations
  // when GenerateObservations is called for the second time on the same day.
  std::map<uint32_t, uint64_t> expected_num_obs;
  std::map<uint32_t, uint64_t> expect_no_obs;
  for (const auto& pair : kNumAggregatedObservations) {
    expected_num_obs[pair.first] = (1 + backfill_days) * pair.second;
    expect_no_obs[pair.first] = 0;
  }

  auto day_index = CurrentDayIndex(clock);
  for (uint32_t errorNameIndex : kUpdateDurationNewErrorNameIndices) {
    for (uint32_t stageIndex : kUpdateDurationNewStageIndices) {
      for (int64_t value : kUpdateDurationNewValues) {
        if (!logger->LogInteger(cobalt_registry::kUpdateDurationNewMetricId, {errorNameIndex, stageIndex}, value)) {
          FX_LOGS(INFO) << "LogInteger(" << cobalt_registry::kUpdateDurationNewMetricId << ", {"
                        << errorNameIndex << ", " << stageIndex << "}, " << value << ")";
          FX_LOGS(INFO) << "TestLogInteger : FAIL";
          return false;
        }
        expected_num_obs[cobalt_registry::kUpdateDurationNewUpdateDurationTimingStatsReportId] +=
            kStreamingTimeNumWindowSizes;
      }
    }
  }

  if (CurrentDayIndex(clock) != day_index) {
    // TODO(fxb/52750) The date has changed mid-test. We are currently unable to
    // deal with this so we fail this test and our caller may try again.
    FX_LOGS(INFO) << "Quitting test because the date has changed mid-test.";
    return false;
  }

  if (!GenerateObsAndCheckCount(day_index, cobalt_controller, expected_num_obs)) {
    FX_LOGS(INFO) << "TestLogInteger : FAIL";
    return false;
  }
  if (!GenerateObsAndCheckCount(day_index, cobalt_controller, expect_no_obs)) {
    FX_LOGS(INFO) << "TestLogInteger : FAIL";
    return false;
  }
  return SendAndCheckSuccess("TestLogInteger", logger);
}

}  // namespace testapp
}  // namespace cobalt
