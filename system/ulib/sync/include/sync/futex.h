// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifdef __cplusplus
#include <fbl/atomic.h>
#else
#include <stdatomic.h>
#endif

typedef struct futex_t {
#ifdef __cplusplus
    fbl::atomic<int> futex;
    explicit futex_t(int initial) : futex(initial) {}
#else
    atomic_int futex;
#endif
} futex_t;
