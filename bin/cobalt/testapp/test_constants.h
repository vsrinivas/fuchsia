// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_COBALT_TESTAPP_TEST_CONSTANTS_H
#define GARNET_BIN_COBALT_TESTAPP_TEST_CONSTANTS_H

#include <string>

namespace cobalt {
namespace testapp {

  // For the rare event with strings test
  const uint32_t kRareEventStringMetricId = 1;
  const uint32_t kRareEventStringEncodingId = 1;
  const std::string kRareEvent1 = "Ledger-startup";

  // For the module views test
  const uint32_t kModuleViewsMetricId = 2;
  const uint32_t kModuleViewsEncodingId = 2;
  const std::string kAModuleUri = "www.cobalt_test_app.com";

  // For the rare event with indexes test
  const uint32_t kRareEventIndexMetricId = 3;
  const uint32_t kRareEventIndexEncodingId = 3;
  constexpr uint32_t kRareEventIndicesToUse[] = {0, 1, 2, 6};

  // For the module pairs test
  const uint32_t kModulePairsMetricId = 4;
  const uint32_t kModulePairsEncodingId = 4;
  const std::string kExistingModulePartName = "existing_module";
  const std::string kAddedModulePartName = "added_module";

  // For the num-stars-in-sky test
  const uint32_t kNumStarsMetricId = 5;
  const uint32_t kNumStarsEncodingId = 4;

  // For the average-read-time test
  const uint32_t kAvgReadTimeMetricId = 6;
  const uint32_t kAvgReadTimeEncodingId = 4;

  // For the spaceship velocity test.
  const uint32_t kSpaceshipVelocityMetricId = 7;
  const uint32_t kSpaceshipVelocityEncodingId = 4;

  // For mod initialisation time.
  const std::string kModTimerId = "test_mod_timer";
  const uint32_t kModTimerMetricId = 8;
  const uint32_t kModTimerEncodingId = 4;
  const uint64_t kModStartTimestamp = 40;
  const uint64_t kModEndTimestamp = 75;
  const uint32_t kModTimeout = 1;

  // For app startup time.
  const std::string kAppTimerId = "test_app_timer";
  const uint32_t kAppTimerMetricId = 9;
  const uint32_t kAppTimerEncodingId = 4;
  const std::string kAppTimerPartName = "time_ns";
  const uint64_t kAppStartTimestamp = 10;
  const uint64_t kAppEndTimestamp = 20;
  const uint32_t kAppTimeout = 2;
  const std::string kAppName = "hangouts";
  const std::string kAppPartName = "app_name";
  const uint32_t kAppNameEncodingId = 4;

  // For testing V1_BACKEND.
  const uint32_t kV1BackendMetricId = 10;
  const uint32_t kV1BackendEncodingId = 4;
  const std::string kV1BackendEvent = "Send-to-V1";

  // For V1 elapsed times.
  const uint32_t kElapsedTimeMetricId = 11;
  const uint32_t kElapsedTimeEventIndex = 0;
  const std::string kElapsedTimeComponent = "some_component";
  const int64_t kElapsedTime = 30;

  // For V1 frame rates.
  const uint32_t kFrameRateMetricId = 12;
  const std::string kFrameRateComponent = "some_component";
  const float kFrameRate = 45.5;

  // For V1 memory usage.
  const uint32_t kMemoryUsageMetricId = 13;
  const uint32_t kMemoryUsageIndex = 1;
  const int64_t kMemoryUsage = 1000000;

  // For events that happened in specific components
  const uint32_t kEventInComponentMetricId = 14;
  const uint32_t kEventInComponentIndex = 2;
  const std::string kEventInComponentName = "some_component";

}  // namespace testapp
}  // namespace cobalt

#endif  // GARNET_BIN_COBALT_TESTAPP_TEST_CONSTANTS_H
