// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#ifndef MAGMA_DLOG_ENABLE
#define MAGMA_DLOG_ENABLE 0
#endif

#define DLOG(...)                                                                                  \
    do {                                                                                           \
        if (MAGMA_DLOG_ENABLE) {                                                                   \
            printf("%s:%d ", __FILE__, __LINE__);                                                  \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)
