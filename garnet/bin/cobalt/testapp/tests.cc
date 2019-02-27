// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cobalt/testapp/tests.h"
#include "garnet/bin/cobalt/testapp/cobalt_metrics.cb.h"
#include "garnet/bin/cobalt/testapp/test_constants.h"

namespace cobalt {
namespace testapp {

using fidl::VectorPtr;
using fuchsia::cobalt::Status;

namespace legacy {

bool TestLogEvent(CobaltTestAppLogger* logger) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "legacy::TestLogEvent";
  bool use_request_send_soon = true;
  for (uint32_t index : kRareEventIndicesToUse) {
    if (!logger->LogEventAndSend(kRareEventIndexMetricId, index,
                                 use_request_send_soon)) {
      FXL_LOG(INFO) << "legacy::TestLogEvent: FAIL";
      return false;
    }
  }
  FXL_LOG(INFO) << "legacy::TestLogEvent: PASS";
  return true;
}

bool TestLogEventUsingServiceFromEnvironment(CobaltTestAppLogger* logger) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "legacy::TestLogEventUsingServiceFromEnvironment";
  // We don't actually use the network in this test strategy because we
  // haven't constructed the Cobalt service ourselves and so we haven't had
  // the opportunity to configure the scheduling parameters.
  bool save_use_network_value = logger->use_network_;
  logger->use_network_ = false;
  for (uint32_t index : kRareEventIndicesToUse) {
    if (!logger->LogEventAndSend(kRareEventIndexMetricId, index, false)) {
      FXL_LOG(INFO) << "legacy::TestLogEventUsingServiceFromEnvironment: FAIL";
      return false;
    }
  }
  FXL_LOG(INFO) << "legacy::TestLogEventUsingServiceFromEnvironment: PASS";
  logger->use_network_ = save_use_network_value;
  return true;
}

bool TestLogEventCount(CobaltTestAppLogger* logger) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "legacy::TestLogEventCount";
  bool use_request_send_soon = true;
  bool success = logger->LogEventCountAndSend(
      kEventInComponentMetricId, kEventInComponentIndex, kEventInComponentName,
      1, use_request_send_soon);

  FXL_LOG(INFO) << "legacy::TestLogEventCount : "
                << (success ? "PASS" : "FAIL");
  return success;
}

bool TestLogElapsedTime(CobaltTestAppLogger* logger) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "legacy::TestLogElapsedTime";
  bool use_request_send_soon = true;
  bool success = logger->LogElapsedTimeAndSend(
      kElapsedTimeMetricId, kElapsedTimeEventIndex, kElapsedTimeComponent,
      kElapsedTime, use_request_send_soon);
  success = success &&
            logger->LogElapsedTimeAndSend(kModTimerMetricId, 0, "",
                                          kModEndTimestamp - kModStartTimestamp,
                                          use_request_send_soon);
  FXL_LOG(INFO) << "legacy::TestLogElapsedTime : "
                << (success ? "PASS" : "FAIL");
  return success;
}

bool TestLogFrameRate(CobaltTestAppLogger* logger) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "legacy::TestLogFrameRate";
  bool use_request_send_soon = true;
  bool success = logger->LogFrameRateAndSend(
      kFrameRateMetricId, kFrameRateEventIndex, kFrameRateComponent, kFrameRate,
      use_request_send_soon);

  FXL_LOG(INFO) << "legacy::TestLogFrameRate : " << (success ? "PASS" : "FAIL");
  return success;
}

bool TestLogMemoryUsage(CobaltTestAppLogger* logger) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "legacy::TestLogMemoryUsage";
  bool use_request_send_soon = true;
  bool success =
      logger->LogMemoryUsageAndSend(kMemoryUsageMetricId, kMemoryUsageIndex, "",
                                    kMemoryUsage, use_request_send_soon);

  FXL_LOG(INFO) << "legacy::TestLogMemoryUsage : "
                << (success ? "PASS" : "FAIL");
  return success;
}

bool TestLogString(CobaltTestAppLogger* logger) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "legacy::TestLogString";
  bool use_request_send_soon = true;
  bool success = logger->LogStringAndSend(kRareEventStringMetricId, kRareEvent1,
                                          use_request_send_soon);
  FXL_LOG(INFO) << "legacy::TestLogString : " << (success ? "PASS" : "FAIL");
  return success;
}

bool TestLogStringUsingBlockUntilEmpty(CobaltTestAppLogger* logger) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "legacy::TestLogStringUsingBlockUntilEmpty";
  bool use_request_send_soon = false;
  bool success = logger->LogStringAndSend(kRareEventStringMetricId, kRareEvent1,
                                          use_request_send_soon);
  FXL_LOG(INFO) << "legacy::TestLogStringUsingBlockUntilEmpty : "
                << (success ? "PASS" : "FAIL");
  return success;
}

bool TestLogTimer(CobaltTestAppLogger* logger) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "legacy::TestLogTimer";
  bool use_request_send_soon = true;
  bool success = logger->LogTimerAndSend(kModTimerMetricId, kModStartTimestamp,
                                         kModEndTimestamp, kModTimerId,
                                         kModTimeout, use_request_send_soon);
  FXL_LOG(INFO) << "legacy::TestLogTimer : " << (success ? "PASS" : "FAIL");
  return success;
}

bool TestLogIntHistogram(CobaltTestAppLogger* logger) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "legacy::TestLogIntHistogram";
  bool use_request_send_soon = true;
  std::map<uint32_t, uint64_t> histogram = {{1, 20}, {3, 20}};
  bool success = logger->LogIntHistogramAndSend(
      kSpaceshipVelocityMetricId, 0, "", histogram, use_request_send_soon);
  FXL_LOG(INFO) << "legacy::TestLogIntHistogram : "
                << (success ? "PASS" : "FAIL");
  return success;
}

bool TestLogCustomEvent(CobaltTestAppLogger* logger) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "legacy::TestLogCustomEvent";
  bool use_request_send_soon = true;
  bool success = logger->LogStringPairAndSend(
      kModulePairsMetricId, kExistingModulePartName, "ModA",
      kAddedModulePartName, "ModB", use_request_send_soon);
  FXL_LOG(INFO) << "legacy::TestLogCustomEvent : "
                << (success ? "PASS" : "FAIL");
  return success;
}

}  // namespace legacy

namespace {
bool SendAndCheckSuccess(const std::string& test_name,
                         bool use_request_send_soon,
                         CobaltTestAppLogger* logger) {
  if (!logger->CheckForSuccessfulSend(use_request_send_soon)) {
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
  bool use_request_send_soon = true;
  for (uint32_t index : kErrorOccurredIndicesToUse) {
    if (!logger->LogEvent(metrics::kErrorOccurredMetricId, index)) {
      FXL_LOG(INFO) << "TestLogEvent: FAIL";
      return false;
    }
  }
  if (logger->LogEvent(metrics::kErrorOccurredMetricId,
                       kErrorOccurredInvalidIndex)) {
    FXL_LOG(INFO) << "TestLogEvent: FAIL";
    return false;
  }

  return SendAndCheckSuccess("TestLogEvent", use_request_send_soon, logger);
}

// file_system_cache_misses using EVENT_COUNT metric.
//
// For each |event_code| and each |component_name|, log one observation with
// a value of kFileSystemCacheMissesCountMax - event_code index.
bool TestLogEventCount(CobaltTestAppLogger* logger) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogEventCount";
  bool use_request_send_soon = true;
  for (uint32_t index : kFileSystemCacheMissesIndices) {
    for (std::string name : kFileSystemCacheMissesComponentNames) {
      if (!logger->LogEventCount(metrics::kFileSystemCacheMissesMetricId, index,
                                 name,
                                 kFileSystemCacheMissesCountMax - index)) {
        FXL_LOG(INFO) << "LogEventCount("
                      << metrics::kFileSystemCacheMissesMetricId << ", "
                      << index << ", " << name << ", "
                      << kFileSystemCacheMissesCountMax - index << ")";
        FXL_LOG(INFO) << "TestLogEventCount: FAIL";
        return false;
      }
    }
  }

  return SendAndCheckSuccess("TestLogEventCount", use_request_send_soon,
                             logger);
}

// update_duration using ELAPSED_TIME metric.
//
// For each |event_code| and each |component_name|, log one observation in each
// exponential histogram bucket.
bool TestLogElapsedTime(CobaltTestAppLogger* logger) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogElapsedTime";
  bool use_request_send_soon = true;
  for (uint32_t index : kUpdateDurationIndices) {
    for (std::string name : kUpdateDurationComponentNames) {
      for (int64_t value : kUpdateDurationValues) {
        if (!logger->LogElapsedTime(metrics::kUpdateDurationMetricId, index,
                                    name, value)) {
          FXL_LOG(INFO) << "LogElapsedTime(" << metrics::kUpdateDurationMetricId
                        << ", " << index << ", " << name << ", " << value
                        << ")";
          FXL_LOG(INFO) << "TestLogElapsedTime: FAIL";
          return false;
        }
      }
    }
  }

  return SendAndCheckSuccess("TestLogElapsedTime", use_request_send_soon,
                             logger);
}

// game_frame_rate using FRAME_RATE metric.
//
// For each |event_code| and each |component_name|, log one observation in each
// exponential histogram bucket.
bool TestLogFrameRate(CobaltTestAppLogger* logger) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogFrameRate";
  bool use_request_send_soon = true;
  for (uint32_t index : kGameFrameRateIndices) {
    for (std::string name : kGameFrameRateComponentNames) {
      for (float value : kGameFrameRateValues) {
        if (!logger->LogFrameRate(metrics::kGameFrameRateMetricId, index, name,
                                  value)) {
          FXL_LOG(INFO) << "LogFrameRate(" << metrics::kGameFrameRateMetricId
                        << ", " << index << ", " << name << ", " << value
                        << ")";
          FXL_LOG(INFO) << "TestLogFrameRate: FAIL";
          return false;
        }
      }
    }
  }

  return SendAndCheckSuccess("TestLogFrameRate", use_request_send_soon, logger);
}

// application_memory
//
// For each |event_code| and each |component_name|, log one observation in each
// exponential histogram bucket.
bool TestLogMemoryUsage(CobaltTestAppLogger* logger) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogMemoryUsage";
  bool use_request_send_soon = true;
  for (uint32_t index : kApplicationMemoryIndices) {
    for (std::string name : kApplicationComponentNames) {
      for (int64_t value : kApplicationMemoryValues) {
        if (!logger->LogMemoryUsage(metrics::kApplicationMemoryMetricId, index,
                                    name, value)) {
          FXL_LOG(INFO) << "LogMemoryUsage("
                        << metrics::kApplicationMemoryMetricId << ", " << index
                        << ", " << name << ", " << value << ")";
          FXL_LOG(INFO) << "TestLogMemoryUsage: FAIL";
          return false;
        }
      }
    }
  }

  return SendAndCheckSuccess("TestLogMemoryUsage", use_request_send_soon,
                             logger);
}

// power_usage and bandwidth_usage
//
// For each |event_code| and each |component_name|, log one observation in each
// histogram bucket, using decreasing values per bucket.
bool TestLogIntHistogram(CobaltTestAppLogger* logger) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogIntHistogram";
  bool use_request_send_soon = true;
  std::map<uint32_t, uint64_t> histogram;

  // Set up and send power_usage histogram.
  for (uint32_t bucket = 0; bucket < kPowerUsageBuckets; bucket++) {
    histogram[bucket] = kPowerUsageBuckets - bucket + 1;
  }
  for (uint32_t index : kPowerUsageIndices) {
    for (std::string name : kApplicationComponentNames) {
      if (!logger->LogIntHistogram(metrics::kPowerUsageMetricId, index, name,
                                   histogram)) {
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
      if (!logger->LogIntHistogram(metrics::kBandwidthUsageMetricId, index,
                                   name, histogram)) {
        FXL_LOG(INFO) << "TestLogIntHistogram : FAIL";
        return false;
      }
    }
  }

  return SendAndCheckSuccess("TestLogIntHistogram", use_request_send_soon,
                             logger);
}

bool TestLogCustomEvent(CobaltTestAppLogger* logger) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogCustomEvent";
  bool use_request_send_soon = true;
  bool success = logger->LogCustomMetricsTestProtoAndSend(
      metrics::kQueryResponseMetricId, "test", 100, 1, use_request_send_soon);

  FXL_LOG(INFO) << "TestLogCustomEvent : " << (success ? "PASS" : "FAIL");
  return success;
}

}  // namespace testapp
}  // namespace cobalt
