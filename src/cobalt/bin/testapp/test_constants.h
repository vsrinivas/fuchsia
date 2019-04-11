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
const std::string kRareEvent1 = "Ledger-startup";

// For the rare event with indexes test
const uint32_t kRareEventIndexMetricId = 3;
constexpr uint32_t kRareEventIndicesToUse[] = {0, 1, 2, 6};

// For the module pairs test
const uint32_t kModulePairsMetricId = 4;
const std::string kExistingModulePartName = "existing_module";
const std::string kAddedModulePartName = "added_module";

// For the spaceship velocity test.
const uint32_t kSpaceshipVelocityMetricId = 7;

// For mod initialisation time.
const std::string kModTimerId = "test_mod_timer";
const uint32_t kModTimerMetricId = 8;
const uint64_t kModStartTimestamp = 40;
const uint64_t kModEndTimestamp = 75;
const uint32_t kModTimeout = 1;

// For V1 elapsed times.
const uint32_t kElapsedTimeMetricId = 11;
const uint32_t kElapsedTimeEventIndex = 0;
const std::string kElapsedTimeComponent = "some_component";
const int64_t kElapsedTime = 30;

// For V1 frame rates.
const uint32_t kFrameRateMetricId = 12;
const uint32_t kFrameRateEventIndex = 0;
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

const uint32_t kErrorOccurredIndicesToUse[] = {0, 1, 2, 9};
const uint32_t kErrorOccurredInvalidIndex = 18;

const uint32_t kErrorOccurredIndex = 2;

// Common metric constants.
const std::string kApplicationComponentNames[] = {
    "audio_core", "chromium",     "logger", "netstack", "scheduler",
    "sysmng",     "view_manager", "",       "unknown"};

// file_system_cache_misses metric constants.
const uint32_t kFileSystemCacheMissesIndices[] = {0, 1};
const std::string kFileSystemCacheMissesComponentNames[] = {
    "blobfs", "minfs", "thinfs", "", "unknown"};
const int64_t kFileSystemCacheMissesCountMax = 2;

// update_duration metric constants.
const int32_t kUpdateDurationIndices[] = {0, 1, 2, 3};
const std::string kUpdateDurationComponentNames[] = {
    "DownloadPayload", "VerifyPayload", "ApplyPayload", "VerifyUpdate", "",
    "unknown"};
const int64_t kUpdateDurationValues[] = {-1, 0,  1,  2,   5,   10,
                                         22, 37, 64, 158, 301, 702};

// game_frame_rate metric constants.
const int32_t kGameFrameRateIndices[] = {0, 1, 2};
const std::string kGameFrameRateComponentNames[] = {
    "Forest", "Beach", "Dungeon", "City", "", "unknown"};
const float kGameFrameRateValues[] = {0.50,   1.23,    3.042,  7.999,
                                      8.0,    25.4,    55.1,   64.003,
                                      201.21, 415.235, 600.001};

// application_memory metric constants
const int32_t kApplicationMemoryIndices[] = {0, 1, 2};
const int64_t kApplicationMemoryValues[] = {
    0, 1000, 2000, 4000, 8000, 16000, 32000, 64000, 128000, 256000, 512000};

// power_usage metric constants.
const int32_t kPowerUsageIndices[] = {0, 1};
const int32_t kPowerUsageBuckets = 52;

// bandwidth_usage metric constants.
const int32_t kBandwidthUsageIndices[] = {0, 1};
const int64_t kBandwidthUsageBuckets = 22;

// features_active metric constants.
const int32_t kFeaturesActiveIndices[] = {0, 1, 2, 3, 9};
const int32_t kFeaturesActiveInvalidIndex = 20;

// connection_attempts metric constants.
const int32_t kConnectionAttemptsIndices[] = {0, 1};
const std::string kConnectionAttemptsComponentNames[] = {"HostA", "HostB",
                                                         "HostC"};
const int kConnectionAttemptsNumWindowSizes = 2;

// streaming_time metric constants.
const int32_t kStreamingTimeIndices[] = {0, 1, 2};
const std::string kStreamingTimeComponentNames[] = {"ModuleA", "ModuleB",
                                                    "ModuleC"};
const int kStreamingTimeNumWindowSizes = 2;

// The number of locally aggregated observations that should be generated for
// the test_app2 project for a day, assuming that no events were logged for
// locally aggregated metrics on that day.
//
// This number is calculated as follows:
//
// features_active: 20 UniqueActivesObservations
//                  (10 event codes * 2 window sizes)
// connection_attempts: 1 ReportParticipationObservation
// streaming_time: 1 ReportParticipationObservation
const int64_t kNumAggregatedObservations = 22;

const size_t kNumBackfillDays = 2;

}  // namespace testapp
}  // namespace cobalt

#endif  // GARNET_BIN_COBALT_TESTAPP_TEST_CONSTANTS_H
