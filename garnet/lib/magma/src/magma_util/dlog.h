// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

// Files #including dlog.h may assume that it #includes inttypes.h.
// So, for convenience, they don't need to follow "#include-what-you-use" for that header.
#include <inttypes.h>

#ifndef MAGMA_DLOG_ENABLE
#define MAGMA_DLOG_ENABLE 0
#endif

#define DLOG(format, ...)                                                                     \
  do {                                                                                        \
    if (MAGMA_DLOG_ENABLE) {                                                                  \
      magma::PlatformLogger::Log(magma::PlatformLogger::LOG_INFO, "%s:%d: " format, __FILE__, \
                                 __LINE__, ##__VA_ARGS__);                                    \
    }                                                                                         \
  } while (0)
