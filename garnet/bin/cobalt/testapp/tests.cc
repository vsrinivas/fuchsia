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

bool TestLogEvent(CobaltTestAppLogger* logger_) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "legacy::TestLogEvent";
  bool use_request_send_soon = true;
  for (uint32_t index : kRareEventIndicesToUse) {
    if (!logger_->LogEventAndSend(kRareEventIndexMetricId, index,
                                  use_request_send_soon)) {
      FXL_LOG(INFO) << "legacy::TestLogEvent: FAIL";
      return false;
    }
  }
  FXL_LOG(INFO) << "legacy::TestLogEvent: PASS";
  return true;
}

bool TestLogEventUsingServiceFromEnvironment(CobaltTestAppLogger* logger_) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "legacy::TestLogEventUsingServiceFromEnvironment";
  // We don't actually use the network in this test strategy because we
  // haven't constructed the Cobalt service ourselves and so we haven't had
  // the opportunity to configure the scheduling parameters.
  bool save_use_network_value = logger_->use_network_;
  logger_->use_network_ = false;
  for (uint32_t index : kRareEventIndicesToUse) {
    if (!logger_->LogEventAndSend(kRareEventIndexMetricId, index, false)) {
      FXL_LOG(INFO) << "legacy::TestLogEventUsingServiceFromEnvironment: FAIL";
      return false;
    }
  }
  FXL_LOG(INFO) << "legacy::TestLogEventUsingServiceFromEnvironment: PASS";
  logger_->use_network_ = save_use_network_value;
  return true;
}

bool TestLogEventCount(CobaltTestAppLogger* logger_) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "legacy::TestLogEventCount";
  bool use_request_send_soon = true;
  bool success = logger_->LogEventCountAndSend(
      kEventInComponentMetricId, kEventInComponentIndex, kEventInComponentName,
      1, use_request_send_soon);

  FXL_LOG(INFO) << "legacy::TestLogEventCount : "
                << (success ? "PASS" : "FAIL");
  return true;
}

bool TestLogElapsedTime(CobaltTestAppLogger* logger_) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "legacy::TestLogElapsedTime";
  bool use_request_send_soon = true;
  bool success = logger_->LogElapsedTimeAndSend(
      kElapsedTimeMetricId, kElapsedTimeEventIndex, kElapsedTimeComponent,
      kElapsedTime, use_request_send_soon);
  success = success &&
            logger_->LogElapsedTimeAndSend(
                kModTimerMetricId, 0, "", kModEndTimestamp - kModStartTimestamp,
                use_request_send_soon);
  FXL_LOG(INFO) << "legacy::TestLogElapsedTime : "
                << (success ? "PASS" : "FAIL");
  return success;
}

bool TestLogFrameRate(CobaltTestAppLogger* logger_) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "legacy::TestLogFrameRate";
  bool use_request_send_soon = true;
  bool success =
      logger_->LogFrameRateAndSend(kFrameRateMetricId, kFrameRateComponent,
                                   kFrameRate, use_request_send_soon);

  FXL_LOG(INFO) << "legacy::TestLogFrameRate : " << (success ? "PASS" : "FAIL");
  return success;
}

bool TestLogMemoryUsage(CobaltTestAppLogger* logger_) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "legacy::TestLogMemoryUsage";
  bool use_request_send_soon = true;
  bool success =
      logger_->LogMemoryUsageAndSend(kMemoryUsageMetricId, kMemoryUsageIndex,
                                     kMemoryUsage, use_request_send_soon);

  FXL_LOG(INFO) << "legacy::TestLogFrameRate : " << (success ? "PASS" : "FAIL");
  return success;
}

bool TestLogString(CobaltTestAppLogger* logger_) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "legacy::TestLogString";
  bool use_request_send_soon = true;
  bool success = logger_->LogStringAndSend(kRareEventStringMetricId,
                                           kRareEvent1, use_request_send_soon);
  FXL_LOG(INFO) << "legacy::TestLogString : " << (success ? "PASS" : "FAIL");
  return success;
}

bool TestLogStringUsingBlockUntilEmpty(CobaltTestAppLogger* logger_) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "legacy::TestLogStringUsingBlockUntilEmpty";
  bool use_request_send_soon = false;
  bool success = logger_->LogStringAndSend(kRareEventStringMetricId,
                                           kRareEvent1, use_request_send_soon);
  FXL_LOG(INFO) << "legacy::TestLogStringUsingBlockUntilEmpty : "
                << (success ? "PASS" : "FAIL");
  return success;
}

bool TestLogTimer(CobaltTestAppLogger* logger_) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "legacy::TestLogTimer";
  bool use_request_send_soon = true;
  bool success = logger_->LogTimerAndSend(kModTimerMetricId, kModStartTimestamp,
                                          kModEndTimestamp, kModTimerId,
                                          kModTimeout, use_request_send_soon);
  FXL_LOG(INFO) << "legacy::TestLogTimer : " << (success ? "PASS" : "FAIL");
  return success;
}

bool TestLogIntHistogram(CobaltTestAppLogger* logger_) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "legacy::TestLogIntHistogram";
  bool use_request_send_soon = true;
  std::map<uint32_t, uint64_t> histogram = {{1, 20}, {3, 20}};
  bool success = logger_->LogIntHistogramAndSend(
      kSpaceshipVelocityMetricId, histogram, use_request_send_soon);
  FXL_LOG(INFO) << "legacy::TestLogIntHistogram : "
                << (success ? "PASS" : "FAIL");
  return success;
}

bool TestLogCustomEvent(CobaltTestAppLogger* logger_) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "legacy::TestLogCustomEvent";
  bool use_request_send_soon = true;
  bool success = logger_->LogStringPairAndSend(
      kModulePairsMetricId, kExistingModulePartName, "ModA",
      kAddedModulePartName, "ModB", use_request_send_soon);
  FXL_LOG(INFO) << "legacy::TestLogCustomEvent : "
                << (success ? "PASS" : "FAIL");
  return success;
}

}  // namespace legacy

bool TestLogEvent(CobaltTestAppLogger* logger_) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogEvent";
  bool use_request_send_soon = true;
  for (uint32_t index : kErrorOccurredIndicesToUse) {
    if (!logger_->LogEventAndSend(metrics::kErrorOccurredMetricId, index,
                                  use_request_send_soon)) {
      FXL_LOG(INFO) << "TestLogEvent: FAIL";
      return false;
    }
  }
  if (logger_->LogEventAndSend(metrics::kErrorOccurredMetricId,
                               kErrorOccurredInvalidIndex,
                               use_request_send_soon)) {
    FXL_LOG(INFO) << "TestLogEvent: FAIL";
    return false;
  }
  FXL_LOG(INFO) << "TestLogEvent: PASS";
  return true;
}

bool TestLogCustomEvent(CobaltTestAppLogger* logger_) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogCustomEvent";
  bool use_request_send_soon = true;
  bool success = logger_->LogCustomMetricsTestProtoAndSend(
      metrics::kQueryResponseMetricId, "test", 100, 1, use_request_send_soon);
  FXL_LOG(INFO) << "TestLogCustomEvent : " << (success ? "PASS" : "FAIL");
  return success;
}

}  // namespace testapp
}  // namespace cobalt
