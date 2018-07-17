// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

// These functions perform overflow-safe time arithmetic, clamping to
// ZX_TIME_INFINITE in case of overflow and 0 in case of underflow.
//
// The naming scheme is zx_<first argument>_<operation>_<second argument>.

static inline zx_time_t zx_time_add_duration(zx_time_t time, zx_duration_t duration) {
    zx_time_t x = 0;
    if (unlikely(add_overflow(time, duration, &x))) {
        return ZX_TIME_INFINITE;
    }
    return x;
}

static inline zx_time_t zx_time_sub_duration(zx_time_t time, zx_duration_t duration) {
    zx_time_t x = 0;
    if (unlikely(sub_overflow(time, duration, &x))) {
        return 0;
    }
    return x;
}

static inline zx_duration_t zx_time_sub_time(zx_time_t time1, zx_time_t time2) {
    zx_duration_t x = 0;
    if (unlikely(sub_overflow(time1, time2, &x))) {
        return 0;
    }
    return x;
}
