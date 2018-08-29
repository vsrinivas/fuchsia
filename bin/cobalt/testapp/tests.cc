// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cobalt/testapp/tests.h"
#include "garnet/bin/cobalt/testapp/test_constants.h"

namespace cobalt {
namespace testapp {

using fuchsia::cobalt::ObservationValue;
using fidl::VectorPtr;

bool TestRareEventWithStrings(CobaltTestAppEncoder* encoder_) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestRareEventWithStrings";
  bool use_request_send_soon = true;
  bool success = encoder_->EncodeStringAndSend(
      kRareEventStringMetricId, kRareEventStringEncodingId, kRareEvent1,
      use_request_send_soon);
  FXL_LOG(INFO) << "TestRareEventWithStrings : " << (success ? "PASS" : "FAIL");
  return success;
}

bool TestRareEventWithIndices(CobaltTestAppEncoder* encoder_) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestRareEventWithIndices";
  bool use_request_send_soon = true;
  for (uint32_t index : kRareEventIndicesToUse) {
    if (!encoder_->EncodeIndexAndSend(kRareEventIndexMetricId,
                                     kRareEventIndexEncodingId, index,
                                     use_request_send_soon)) {
      FXL_LOG(INFO) << "TestRareEventWithIndices: FAIL";
      return false;
    }
  }
  FXL_LOG(INFO) << "TestRareEventWithIndices: PASS";
  return true;
}

bool TestModuleUris(CobaltTestAppEncoder* encoder_) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestModuleUris";
  bool use_request_send_soon = true;
  bool success =
      encoder_->EncodeStringAndSend(kModuleViewsMetricId,
                                   kModuleViewsEncodingId, kAModuleUri,
                                   use_request_send_soon);
  FXL_LOG(INFO) << "TestModuleUris : " << (success ? "PASS" : "FAIL");
  return success;
}

bool TestNumStarsInSky(CobaltTestAppEncoder* encoder_) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestNumStarsInSky";
  bool use_request_send_soon = true;
  bool success = encoder_->EncodeIntAndSend(
      kNumStarsMetricId, kNumStarsEncodingId, 42, use_request_send_soon);
  FXL_LOG(INFO) << "TestNumStarsInSky : " << (success ? "PASS" : "FAIL");
  return success;
}

bool TestSpaceshipVelocity(CobaltTestAppEncoder* encoder_) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestSpaceshipVelocity";
  bool use_request_send_soon = true;
  std::map<uint32_t, uint64_t> distribution = {{1, 20}, {3, 20}};
  bool success = encoder_->EncodeIntDistributionAndSend(
      kSpaceshipVelocityMetricId, kSpaceshipVelocityEncodingId, distribution,
      use_request_send_soon);
  FXL_LOG(INFO) << "TestSpaceshipVelocity : " << (success ? "PASS" : "FAIL");
  return success;
}

bool TestAvgReadTime(CobaltTestAppEncoder* encoder_) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestAvgReadTime";
  bool use_request_send_soon = true;
  bool success =
      encoder_->EncodeDoubleAndSend(kAvgReadTimeMetricId, kAvgReadTimeEncodingId,
                                   3.14159, use_request_send_soon);
  FXL_LOG(INFO) << "TestAvgReadTime : " << (success ? "PASS" : "FAIL");
  return success;
}

bool TestModulePairs(CobaltTestAppEncoder* encoder_) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestModuleUriPairs";
  bool use_request_send_soon = true;
  bool success = encoder_->EncodeStringPairAndSend(
      kModulePairsMetricId, kExistingModulePartName, kModulePairsEncodingId,
      "ModA", kAddedModulePartName, kModulePairsEncodingId, "ModB",
      use_request_send_soon);
  FXL_LOG(INFO) << "TestModuleUriPairs : " << (success ? "PASS" : "FAIL");
  return success;
}

bool TestRareEventWithStringsUsingBlockUntilEmpty(
  CobaltTestAppEncoder* encoder_) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestRareEventWithStringsUsingBlockUntilEmpty";
  bool use_request_send_soon = false;
  bool success = encoder_->EncodeStringAndSend(
      kRareEventStringMetricId, kRareEventStringEncodingId, kRareEvent1,
      use_request_send_soon);
  FXL_LOG(INFO) << "TestRareEventWithStringsUsingBlockUntilEmpty : "
                << (success ? "PASS" : "FAIL");
  return success;
}

bool TestRareEventWithIndicesUsingServiceFromEnvironment(
  CobaltTestAppEncoder* encoder_) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestRareEventWithIndicesUsingServiceFromEnvironment";
  // We don't actually use the network in this test strategy because we
  // haven't constructed the Cobalt service ourselves and so we haven't had
  // the opportunity to configure the scheduling parameters.
  bool save_use_network_value = encoder_->use_network_;
  encoder_->use_network_ = false;
  for (uint32_t index : kRareEventIndicesToUse) {
    if (!encoder_->EncodeIndexAndSend(kRareEventIndexMetricId,
                                     kRareEventIndexEncodingId, index, false)) {
      FXL_LOG(INFO)
          << "TestRareEventWithIndicesUsingServiceFromEnvironment: FAIL";
      return false;
    }
  }
  FXL_LOG(INFO) << "TestRareEventWithIndicesUsingServiceFromEnvironment: PASS";
  encoder_->use_network_ = save_use_network_value;
  return true;
}

bool TestModInitializationTime(CobaltTestAppEncoder* encoder_) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestModInitialisationTime";
  bool use_request_send_soon = true;
  bool success = encoder_->EncodeTimerAndSend(
      kModTimerMetricId, kModTimerEncodingId, kModStartTimestamp,
      kModEndTimestamp, kModTimerId, kModTimeout, use_request_send_soon);
  FXL_LOG(INFO) << "TestModInitialisationTime : "
                << (success ? "PASS" : "FAIL");
  return success;
}

bool TestAppStartupTime(CobaltTestAppEncoder* encoder_) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestAppStartupTime";
  bool use_request_send_soon = true;
  bool success = encoder_->EncodeMultipartTimerAndSend(
      kAppTimerMetricId, kAppPartName, kAppNameEncodingId, kAppName,
      kAppTimerPartName, kAppTimerEncodingId, kAppStartTimestamp,
      kAppEndTimestamp, kAppTimerId, kAppTimeout, use_request_send_soon);
  FXL_LOG(INFO) << "TestAppStartupTime : " << (success ? "PASS" : "FAIL");
  return success;
}

bool TestV1Backend(CobaltTestAppEncoder* encoder_) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestV1Backend";
  bool use_request_send_soon = true;
  bool success = encoder_->EncodeStringAndSend(
      kV1BackendMetricId, kV1BackendEncodingId, kV1BackendEvent,
      use_request_send_soon);
  FXL_LOG(INFO) << "TestV1Backend : " << (success ? "PASS" : "FAIL");
  return success;
}


bool TestLogEvent(CobaltTestAppLogger* logger_) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogEvent";
  bool use_request_send_soon = true;
  for (uint32_t index : kRareEventIndicesToUse) {
    if (!logger_->LogEventAndSend(kRareEventIndexMetricId, index,
                         use_request_send_soon)) {
      FXL_LOG(INFO) << "TestLogEvent: FAIL";
      return false;
    }
  }
  FXL_LOG(INFO) << "TestLogEvent: PASS";
  return true;
}

bool TestLogEventCount(CobaltTestAppLogger* logger_) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogEventCount";
  bool use_request_send_soon = true;
  bool success =
      logger_->LogEventCountAndSend(kEventInComponentMetricId, kEventInComponentIndex,
                           kEventInComponentName, 1, use_request_send_soon);

  FXL_LOG(INFO) << "TestLogEventCount : " << (success ? "PASS" : "FAIL");
  return true;
}

bool TestLogElapsedTime(CobaltTestAppLogger* logger_) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogElapsedTime";
  bool use_request_send_soon = true;
  bool success = logger_->LogElapsedTimeAndSend(
      kElapsedTimeMetricId, kElapsedTimeEventIndex, kElapsedTimeComponent,
      kElapsedTime, use_request_send_soon);
  success =
      success && logger_->LogElapsedTimeAndSend(kModTimerMetricId, 0, "",
                                       kModEndTimestamp - kModStartTimestamp,
                                       use_request_send_soon);
  FXL_LOG(INFO) << "TestLogElapsedTime : " << (success ? "PASS" : "FAIL");
  return success;
}

bool TestLogFrameRate(CobaltTestAppLogger* logger_) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogFrameRate";
  bool use_request_send_soon = true;
  bool success = logger_->LogFrameRateAndSend(kFrameRateMetricId, kFrameRateComponent,
                                     kFrameRate, use_request_send_soon);

  FXL_LOG(INFO) << "TestLogFrameRate : " << (success ? "PASS" : "FAIL");
  return success;
}

bool TestLogMemoryUsage(CobaltTestAppLogger* logger_) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogMemoryUsage";
  bool use_request_send_soon = true;
  bool success = logger_->LogMemoryUsageAndSend(kMemoryUsageMetricId, kMemoryUsageIndex,
                                       kMemoryUsage, use_request_send_soon);

  FXL_LOG(INFO) << "TestLogFrameRate : " << (success ? "PASS" : "FAIL");
  return success;
}

bool TestLogString(CobaltTestAppLogger* logger_) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogString";
  bool use_request_send_soon = true;
  bool success = logger_->LogStringAndSend(kRareEventStringMetricId, kRareEvent1,
                                  use_request_send_soon);
  FXL_LOG(INFO) << "TestLogString : " << (success ? "PASS" : "FAIL");
  return success;
}

bool TestLogTimer(CobaltTestAppLogger* logger_) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogTimer";
  bool use_request_send_soon = true;
  bool success =
      logger_->LogTimerAndSend(kModTimerMetricId, kModStartTimestamp, kModEndTimestamp,
                      kModTimerId, kModTimeout, use_request_send_soon);
  FXL_LOG(INFO) << "TestLogTimer : " << (success ? "PASS" : "FAIL");
  return success;
}

bool TestLogCustomEvent(CobaltTestAppLogger* logger_) {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogCustomEvent";
  bool use_request_send_soon = true;
  bool success = logger_->LogStringPairAndSend(
      kModulePairsMetricId, kExistingModulePartName, kModulePairsEncodingId,
      "ModA", kAddedModulePartName, kModulePairsEncodingId, "ModB",
      use_request_send_soon);
  FXL_LOG(INFO) << "TestLogCustomEvent : " << (success ? "PASS" : "FAIL");
  return success;
}

}  // namespace testapp
}  // namespace cobalt
