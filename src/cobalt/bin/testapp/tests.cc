// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/testapp/tests.h"

#include "src/cobalt/bin/testapp/test_constants.h"
#include "src/cobalt/bin/testapp/testapp_metrics_registry.cb.h"
#include "src/lib/cobalt/cpp/cobalt_event_builder.h"
#include "third_party/cobalt/config/metric_definition.pb.h"
#include "third_party/cobalt/util/datetime_util.h"

namespace cobalt {

using util::ClockInterface;
using util::TimeToDayIndex;

namespace testapp {

using fidl::VectorPtr;
using fuchsia::cobalt::Status;

namespace {
uint32_t CurrentDayIndex(ClockInterface* clock) {
  return TimeToDayIndex(std::chrono::system_clock::to_time_t(clock->now()),
                        MetricDefinition::UTC);
}

bool SendAndCheckSuccess(const std::string& test_name,
                         CobaltTestAppLogger* logger) {
  if (!logger->CheckForSuccessfulSend()) {
    FXL_LOG(INFO) << "CheckForSuccessfulSend() returned false";
    FXL_LOG(INFO) << test_name << ": FAIL";
    return false;
  }
  FXL_LOG(INFO) << test_name << ": PASS";
  return true;
}
}  // namespace

bool TestLogEvent(CobaltTestAppLogger* logger) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogEvent";
  for (uint32_t index : kErrorOccurredIndicesToUse) {
    if (!logger->LogEvent(cobalt_registry::kErrorOccurredMetricId, index)) {
      FXL_LOG(INFO) << "TestLogEvent: FAIL";
      return false;
    }
  }
  if (logger->LogEvent(cobalt_registry::kErrorOccurredMetricId,
                       kErrorOccurredInvalidIndex)) {
    FXL_LOG(INFO) << "TestLogEvent: FAIL";
    return false;
  }

  return SendAndCheckSuccess("TestLogEvent", logger);
}

// file_system_cache_misses using EVENT_COUNT metric.
//
// For each |event_code| and each |component_name|, log one observation with
// a value of kFileSystemCacheMissesCountMax - event_code index.
bool TestLogEventCount(CobaltTestAppLogger* logger) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogEventCount";
  for (uint32_t index : kFileSystemCacheMissesIndices) {
    for (std::string name : kFileSystemCacheMissesComponentNames) {
      if (!logger->LogEventCount(
              cobalt_registry::kFileSystemCacheMissesMetricId, index, name,
              kFileSystemCacheMissesCountMax - index)) {
        FXL_LOG(INFO) << "LogEventCount("
                      << cobalt_registry::kFileSystemCacheMissesMetricId << ", "
                      << index << ", " << name << ", "
                      << kFileSystemCacheMissesCountMax - index << ")";
        FXL_LOG(INFO) << "TestLogEventCount: FAIL";
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
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogElapsedTime";
  for (uint32_t index : kUpdateDurationIndices) {
    for (std::string name : kUpdateDurationComponentNames) {
      for (int64_t value : kUpdateDurationValues) {
        if (!logger->LogElapsedTime(cobalt_registry::kUpdateDurationMetricId,
                                    index, name, value)) {
          FXL_LOG(INFO) << "LogElapsedTime("
                        << cobalt_registry::kUpdateDurationMetricId << ", "
                        << index << ", " << name << ", " << value << ")";
          FXL_LOG(INFO) << "TestLogElapsedTime: FAIL";
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
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogFrameRate";
  for (uint32_t index : kGameFrameRateIndices) {
    for (std::string name : kGameFrameRateComponentNames) {
      for (float value : kGameFrameRateValues) {
        if (!logger->LogFrameRate(cobalt_registry::kGameFrameRateMetricId,
                                  index, name, value)) {
          FXL_LOG(INFO) << "LogFrameRate("
                        << cobalt_registry::kGameFrameRateMetricId << ", "
                        << index << ", " << name << ", " << value << ")";
          FXL_LOG(INFO) << "TestLogFrameRate: FAIL";
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
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogMemoryUsage";
  for (uint32_t index : kApplicationMemoryIndices) {
    for (std::string name : kApplicationComponentNames) {
      for (int64_t value : kApplicationMemoryValues) {
        if (!logger->LogMemoryUsage(cobalt_registry::kApplicationMemoryMetricId,
                                    index, name, value)) {
          FXL_LOG(INFO) << "LogMemoryUsage("
                        << cobalt_registry::kApplicationMemoryMetricId << ", "
                        << index << ", " << name << ", " << value << ")";
          FXL_LOG(INFO) << "TestLogMemoryUsage: FAIL";
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
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogIntHistogram";
  std::map<uint32_t, uint64_t> histogram;

  // Set up and send power_usage histogram.
  for (uint32_t bucket = 0; bucket < kPowerUsageBuckets; bucket++) {
    histogram[bucket] = kPowerUsageBuckets - bucket + 1;
  }
  for (uint32_t index : kPowerUsageIndices) {
    for (std::string name : kApplicationComponentNames) {
      if (!logger->LogIntHistogram(cobalt_registry::kPowerUsageMetricId, index,
                                   name, histogram)) {
        FXL_LOG(INFO) << "TestLogIntHistogram : FAIL";
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
      if (!logger->LogIntHistogram(cobalt_registry::kBandwidthUsageMetricId,
                                   index, name, histogram)) {
        FXL_LOG(INFO) << "TestLogIntHistogram : FAIL";
        return false;
      }
    }
  }

  return SendAndCheckSuccess("TestLogIntHistogram", logger);
}

bool TestLogCustomEvent(CobaltTestAppLogger* logger) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogCustomEvent";
  bool success = logger->LogCustomMetricsTestProto(
      cobalt_registry::kQueryResponseMetricId, "test", 100, 1);

  FXL_LOG(INFO) << "TestLogCustomEvent : " << (success ? "PASS" : "FAIL");

  return SendAndCheckSuccess("TestLogCustomEvent", logger);
}

bool TestLogCobaltEvent(CobaltTestAppLogger* logger) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogCobaltEvent";

  if (logger->LogCobaltEvent(
          CobaltEventBuilder(cobalt_registry::kErrorOccurredMetricId)
              .as_event())) {
    // A LogEvent with no event codes is invalid.
    FXL_LOG(INFO) << "TestLogCobaltEvent: FAIL";
    return false;
  }

  if (logger->LogCobaltEvent(
          CobaltEventBuilder(cobalt_registry::kErrorOccurredMetricId)
              .with_event_code(0)
              .with_event_code(0)
              .as_event())) {
    // A LogEvent with more than 1 event code is invalid.
    FXL_LOG(INFO) << "TestLogCobaltEvent: FAIL";
    return false;
  }

  for (uint32_t index : kErrorOccurredIndicesToUse) {
    if (!logger->LogCobaltEvent(
            CobaltEventBuilder(cobalt_registry::kErrorOccurredMetricId)
                .with_event_code(index)
                .as_event())) {
      FXL_LOG(INFO) << "TestLogCobaltEvent: FAIL";
      return false;
    }
  }

  if (!SendAndCheckSuccess("TestLogCobaltEvent", logger)) {
    return false;
  }

  for (uint32_t index : kFileSystemCacheMissesIndices) {
    for (std::string name : kFileSystemCacheMissesComponentNames) {
      if (!logger->LogCobaltEvent(
              CobaltEventBuilder(
                  cobalt_registry::kFileSystemCacheMissesMetricId)
                  .with_event_code(index)
                  .with_component(name)
                  .as_count_event(0, kFileSystemCacheMissesCountMax - index))) {
        FXL_LOG(INFO) << "TestLogCobaltEvent: FAIL";
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
        if (!logger->LogCobaltEvent(
                CobaltEventBuilder(cobalt_registry::kUpdateDurationMetricId)
                    .with_event_code(index)
                    .with_component(name)
                    .as_elapsed_time(value))) {
          FXL_LOG(INFO) << "LogElapsedTime("
                        << cobalt_registry::kUpdateDurationMetricId << ", "
                        << index << ", " << name << ", " << value << ")";
          FXL_LOG(INFO) << "TestLogCobaltEvent: FAIL";
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
// |expected_num_obs|.
bool GenerateObsAndCheckCount(
    uint32_t day_index, fuchsia::cobalt::ControllerSyncPtr* cobalt_controller,
    int64_t expected_num_obs) {
  FXL_LOG(INFO) << "Generating locally aggregated observations for day index "
                << day_index;
  int64_t num_obs = 0;
  (*cobalt_controller)->GenerateAggregatedObservations(day_index, &num_obs);
  FXL_LOG(INFO) << "Generated " << num_obs
                << " locally aggregated observations.";
  if (num_obs != expected_num_obs) {
    FXL_LOG(INFO) << "Expected " << expected_num_obs << " observations.";
    return false;
  }
  return true;
}

bool TestLogEventWithAggregation(
    CobaltTestAppLogger* logger, ClockInterface* clock,
    fuchsia::cobalt::ControllerSyncPtr* cobalt_controller,
    const size_t backfill_days) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogEventWithAggregation";
  for (uint32_t index : kFeaturesActiveIndices) {
    if (!logger->LogEvent(cobalt_registry::kFeaturesActiveMetricId, index)) {
      FXL_LOG(INFO) << "Failed to log event with index " << index << ".";
      FXL_LOG(INFO) << "TestLogEventWithAggregation : FAIL";
      return false;
    }
  }
  if (logger->LogEvent(cobalt_registry::kFeaturesActiveMetricId,
                       kFeaturesActiveInvalidIndex)) {
    FXL_LOG(INFO) << "Failed to reject event with invalid index "
                  << kFeaturesActiveInvalidIndex << ".";
    FXL_LOG(INFO) << "TestLogEventWithAggregation : FAIL";
    return false;
  }
  if (!GenerateObsAndCheckCount(
          CurrentDayIndex(clock), cobalt_controller,
          kNumAggregatedObservations * (1 + backfill_days))) {
    FXL_LOG(INFO) << "TestLogEventWithAggregation : FAIL";
    return false;
  }
  if (!GenerateObsAndCheckCount(CurrentDayIndex(clock), cobalt_controller, 0)) {
    FXL_LOG(INFO) << "TestLogEventWithAggregation : FAIL";
    return false;
  }
  return SendAndCheckSuccess("TestLogEventWithAggregation", logger);
}

bool TestLogEventCountWithAggregation(
    CobaltTestAppLogger* logger, ClockInterface* clock,
    fuchsia::cobalt::ControllerSyncPtr* cobalt_controller,
    const size_t backfill_days) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogEventCountWithAggregation";
  int expected_num_obs = kNumAggregatedObservations * (1 + backfill_days);
  for (uint32_t index : kConnectionAttemptsIndices) {
    for (std::string component : kConnectionAttemptsComponentNames) {
      if (index != 0) {
        // Log a count depending on the index.
        int64_t count = index * 5;
        if (!logger->LogEventCount(cobalt_registry::kConnectionAttemptsMetricId,
                                   index, component, count)) {
          FXL_LOG(INFO) << "Failed to log event count for index " << index
                        << " and component " << component << ".";
          FXL_LOG(INFO) << "TestLogEventCountWithAggregation : FAIL";
          return false;
        }
        expected_num_obs += kConnectionAttemptsNumWindowSizes;
      }
    }
  }
  if (!GenerateObsAndCheckCount(CurrentDayIndex(clock), cobalt_controller,
                                expected_num_obs)) {
    FXL_LOG(INFO) << "TestLogEventCountWithAggregation : FAIL";
    return false;
  }
  if (!GenerateObsAndCheckCount(CurrentDayIndex(clock), cobalt_controller, 0)) {
    FXL_LOG(INFO) << "TestLogEventCountWithAggregation : FAIL";
    return false;
  }
  return SendAndCheckSuccess("TestLogEventCountWithAggregation", logger);
}

bool TestLogElapsedTimeWithAggregation(
    CobaltTestAppLogger* logger, ClockInterface* clock,
    fuchsia::cobalt::ControllerSyncPtr* cobalt_controller,
    const size_t backfill_days) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogElapsedTimeWithAggregation";
  int expected_num_obs = kNumAggregatedObservations * (1 + backfill_days);
  for (uint32_t index : kStreamingTimeIndices) {
    for (std::string component : kStreamingTimeComponentNames) {
      // Log a duration depending on the index.
      if (index != 0) {
        int64_t duration = index * 100;
        if (!logger->LogElapsedTime(cobalt_registry::kStreamingTimeMetricId,
                                    index, component, duration)) {
          FXL_LOG(INFO) << "Failed to log elapsed time for index " << index
                        << " and component " << component << ".";
          FXL_LOG(INFO) << "TestLogElapsedTimeWithAggregation : FAIL";
          return false;
        }
        expected_num_obs += kStreamingTimeNumWindowSizes;
      }
    }
  }
  if (!GenerateObsAndCheckCount(CurrentDayIndex(clock), cobalt_controller,
                                expected_num_obs)) {
    FXL_LOG(INFO) << "TestLogElapsedTimeWithAggregation : FAIL";
    return false;
  }
  if (!GenerateObsAndCheckCount(CurrentDayIndex(clock), cobalt_controller, 0)) {
    FXL_LOG(INFO) << "TestLogElapsedTimeWithAggregation : FAIL";
    return false;
  }
  return SendAndCheckSuccess("TestLogElapsedTimeWithAggregation", logger);
}

}  // namespace testapp
}  // namespace cobalt
