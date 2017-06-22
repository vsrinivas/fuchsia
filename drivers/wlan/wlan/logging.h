// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdio>

// TODO(tkilbourn): use standard logging infrastructure
namespace wlan {
constexpr uint64_t kLogLevelError   = 1 << 0;
constexpr uint64_t kLogLevelWarning = 1 << 1;
constexpr uint64_t kLogLevelInfo    = 1 << 2;
constexpr uint64_t kLogLevelDebug   = 1 << 3;
constexpr uint64_t kLogLevelVerbose = 1 << 4;

constexpr uint64_t kLogErrors = kLogLevelError;
constexpr uint64_t kLogWarnings = kLogErrors | kLogLevelWarning;
constexpr uint64_t kLogInfos = kLogWarnings | kLogLevelInfo;
constexpr uint64_t kLogDebugs = kLogInfos | kLogLevelDebug;
constexpr uint64_t kLogVerboses = kLogDebugs | kLogLevelVerbose;

// Set this to tune log output
constexpr uint64_t kLogLevel = kLogInfos;

#define wlogf(level, level_prefix, args...) do { \
    if (level & kLogLevel) { \
        std::printf("wlan: " level_prefix args); \
    } \
} while (false)

#define errorf(args...)   wlogf(kLogLevelError,   "[E] ", args)
#define warnf(args...)    wlogf(kLogLevelWarning, "[W] ", args)
#define infof(args...)    wlogf(kLogLevelInfo,    "[I] ", args)
#define debugf(args...)   wlogf(kLogLevelDebug,   "[D] ", args)
#define verbosef(args...) wlogf(kLogLevelVerbose, "[V] ", args)
#define debugfn() debugf("%s\n", __PRETTY_FUNCTION__)
}  // namespace wlan
