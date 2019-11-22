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

// TODO(13095) - use MAGMA_LOG here
#define DLOG(...)                           \
  do {                                      \
    if (MAGMA_DLOG_ENABLE) {                \
      printf("%s:%d ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                  \
      printf("\n");                         \
    }                                       \
  } while (0)
