// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PLATFORM_LOGGER_H
#define PLATFORM_LOGGER_H

#include "platform_handle.h"

namespace magma {

class PlatformLogger {
 public:
  enum LogLevel { LOG_ERROR, LOG_WARNING, LOG_INFO };

  static bool Initialize(std::unique_ptr<PlatformHandle> channel);
  static bool IsInitialized();

  __attribute__((format(printf, 2, 3))) static void Log(PlatformLogger::LogLevel level,
                                                        const char* msg, ...);
};

}  // namespace magma

#define MAGMA_LOG(level, ...) \
  magma::PlatformLogger::Log(magma::PlatformLogger::LOG_##level, __VA_ARGS__)

#endif  // PLATFORM_LOGGER_H
