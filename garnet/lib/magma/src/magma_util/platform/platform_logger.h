// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PLATFORM_LOGGER_H
#define PLATFORM_LOGGER_H

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "platform_handle.h"

#define MAGMA_LOG(level, ...) \
  magma::PlatformLogger::Log(magma::PlatformLogger::LOG_##level, __VA_ARGS__)

namespace magma {

class PlatformLogger {
 public:
  enum LogLevel { LOG_ERROR, LOG_WARNING, LOG_INFO };

  static bool Initialize(std::unique_ptr<PlatformHandle> channel);
  static bool IsInitialized();

  __attribute__((format(printf, 2, 3))) static void Log(PlatformLogger::LogLevel level,
                                                        const char* msg, ...);

  __attribute__((format(printf, 4, 5))) static void LogFrom(PlatformLogger::LogLevel level,
                                                            const char* file, int line,
                                                            const char* msg, ...);
  static constexpr size_t kBufferSize = 512;
  static constexpr size_t kSentinelSize = 4;

  static void FormatBuffer(char buffer_out[kBufferSize], const char* file, int line,
                           const char* msg, va_list args) {
    const char kSentinel[] = "***";
    static_assert(sizeof(kSentinel) == kSentinelSize, "sanity");
    constexpr int kMaxSize = kBufferSize - sizeof(kSentinel);
    strcpy(buffer_out + kMaxSize, kSentinel);

    int size = 0;
    if (file) {
      size += snprintf(buffer_out, kMaxSize, "%s:%d ", file, line);
    }
    if (size < kMaxSize) {
      size += vsnprintf(buffer_out + size, kMaxSize - size, msg, args);
    }
    if (size < kMaxSize) {
      snprintf(buffer_out + size, kMaxSize - size, "\n");
    }
  }
};

}  // namespace magma

#endif  // PLATFORM_LOGGER_H
