// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_METRICS_BUFFER_LOG_H_
#define SRC_LIB_METRICS_BUFFER_LOG_H_

#include <lib/ddk/debug.h>
#include <lib/syslog/global.h>
#include <lib/syslog/logger.h>

#include <string_view>

#define VLOG_ENABLED 0

#if (VLOG_ENABLED)
#define VLOGF(format, ...) LOGF(format, ##__VA_ARGS__)
#else
#define VLOGF(...) \
  do {             \
  } while (0)
#endif

#define LOGF(format, ...)             \
  do {                                \
    LOG(INFO, format, ##__VA_ARGS__); \
  } while (0)

#define LOG(severity, format, ...)                              \
  do {                                                          \
    static_assert(true || DDK_LOG_##severity);                  \
    static_assert(true || FX_LOG_##severity);                   \
    FX_LOGF(severity, "metrics_buffer", format, ##__VA_ARGS__); \
  } while (0)

#endif  // SRC_LIB_METRICS_BUFFER_LOG_H_
