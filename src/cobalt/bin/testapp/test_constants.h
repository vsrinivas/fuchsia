// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_TESTAPP_TEST_CONSTANTS_H_
#define SRC_COBALT_BIN_TESTAPP_TEST_CONSTANTS_H_

#include <string>

#include "src/cobalt/bin/testapp/testapp_metrics_registry.cb.h"

namespace cobalt::testapp {

// Common metric constants.
const std::string kApplicationComponentNames[] = {"audio_core", "logger", "scheduler", "scenic",
                                                  "unknown"};
// update_duration_new metric constants.
const uint32_t kUpdateDurationNewErrorNameIndices[] = {0, 1, 2};
const uint32_t kUpdateDurationNewStageIndices[] = {0, 1};
const int64_t kUpdateDurationNewValues[] = {-1, 0, 1, 10, 702};

// streaming_time_new metric constants.
const uint32_t kStreamingTimeNewTypeIndices[] = {0, 1, 2};
const uint32_t kStreamingTimeNewModuleNameIndices[] = {0, 1};
const int64_t kStreamingTimeNewValues[] = {0, 100, 1000, 10000};

// application_memory_new metric constants
const uint32_t kApplicationMemoryNewMemoryTypeIndices[] = {0, 1, 2};
const uint32_t kApplicationMemoryNewApplicationNameIndices[] = {0, 1};
const int64_t kApplicationMemoryNewValues[] = {0, 1000, 4000, 16000, 128000, 512000};

// features_active_new metric constants.
const uint32_t kFeaturesActiveNewSkillIndices[] = {0, 1, 2, 3};
const int64_t kFeaturesActiveNewCounts[] = {1, 2, 10, 42};

// file_system_cache_misses_new metric constants.
const uint32_t kFileSystemCacheMissesNewEncryptionStateIndices[] = {0, 1};
const uint32_t kFileSystemCacheMissesNewFileSystemTypeIndices[] = {0, 1, 2};
const int64_t kFileSystemCacheMissesNewCounts[] = {0, 1, 100, 1000};

// connection_attempts_new metric constants.
const int32_t kConnectionAttemptsNewStatusIndices[] = {0, 1};
const int32_t kConnectionAttemptsNewHostNameIndices[] = {0, 1, 2};
const int64_t kConnectionAttemptsNewCounts[] = {0, 1, 100, 1000};

// power_usage_new metric constants.
const uint32_t kPowerUsageNewApplicationStateIndices[] = {0, 1};
const uint32_t kPowerUsageNewApplicationNameIndices[] = {0, 1};
const int32_t kPowerUsageNewBuckets = 52;

// bandwidth_usage_new metric constants.
const uint32_t kBandwidthUsageNewApplicationStateIndices[] = {0, 1};
const uint32_t kBandwidthUsageNewApplicationNameIndices[] = {0, 1};
const int64_t kBandwidthUsageNewBuckets = 22;

// error_occurred_new metric constants.
const uint32_t kErrorOccurredNewIndicesToUse[] = {0, 1, 2, 9};
const uint32_t kErrorOccurredNewCounts[] = {0, 1, 100, 1000};

// error_occurred_components metric constants.
const uint32_t kErrorOccurredComponentsStatusIndices[] = {0, 1, 2};

}  // namespace cobalt::testapp

#endif  // SRC_COBALT_BIN_TESTAPP_TEST_CONSTANTS_H_
