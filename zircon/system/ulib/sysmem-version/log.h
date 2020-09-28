// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_SYSMEM_VERSION_LOG_H_
#define ZIRCON_SYSTEM_ULIB_SYSMEM_VERSION_LOG_H_

#include <stdio.h>

#define VLOG_ENABLED 0

#if (VLOG_ENABLED)
#define VLOGF(format, ...) LOG(INFO, format, ##__VA_ARGS__)
#else
#define VLOGF(...) \
  do {             \
  } while (0)
#endif

// Temporary solution for logging in driver and non-driver contexts by logging to stderr
// TODO(fxbug.dev/41539): Replace with logging interface that accommodates both driver and non-driver contexts
// including sysmem driver itself.
#define SYSMEM_VERSION_LOG_TRACE (0x10)
#define SYSMEM_VERSION_LOG_DEBUG (0x20)
#define SYSMEM_VERSION_LOG_INFO (0x30)
#define SYSMEM_VERSION_LOG_WARNING (0x40)
#define SYSMEM_VERSION_LOG_ERROR (0x50)
#define SYSMEM_VERSION_LOG_FATAL (0x60)

#define LOG(severity, format, ...)                                                     \
  do {                                                                                 \
    if (SYSMEM_VERSION_LOG_##severity >= SYSMEM_VERSION_LOG_INFO) {                    \
      fprintf(stderr, "[%s:%s:%d] " format "\n", "sysmem-version", __func__, __LINE__, \
              ##__VA_ARGS__);                                                          \
    }                                                                                  \
  } while (0)

#endif  // ZIRCON_SYSTEM_ULIB_SYSMEM_VERSION_LOG_H_
