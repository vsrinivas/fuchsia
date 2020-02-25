// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/syslog/logger.h>

#define VLOG_ENABLED 0

#if (VLOG_ENABLED)
#define VLOGF(format, ...) LOGF(format, ##__VA_ARGS__)
#else
#define VLOGF(...) \
  do {             \
  } while (0)
#endif

#define LOGF(format, ...) \
  do { \
    fprintf(stderr, "[%s:%s:%d] " format "\n", "codec_impl", __func__, __LINE__, ##__VA_ARGS__); \
  } while(0)

// Temporary solution for logging in driver and non-driver contexts by logging to stderr
// TODO(41539): Replace with logging interface that accommodates both driver and non-driver contexts
#define LOG(severity, format, ...)      \
  do { \
    if (FX_LOG_##severity >= FX_LOG_INFO) { \
      fprintf(stderr, "[%s:%s:%d]" format "\n", "codec_impl", __func__, __LINE__, ##__VA_ARGS__); \
    } \
  } while(0)
