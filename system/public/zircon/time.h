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
//
// TODO(maniscalco): Consider expanding the set of operations to include division, modulo, unit
// conversion, and floating point math.

__CONSTEXPR static inline zx_time_t zx_time_add_duration(zx_time_t time, zx_duration_t duration) {
    zx_time_t x = 0;
    if (unlikely(add_overflow(time, duration, &x))) {
        return ZX_TIME_INFINITE;
    }
    return x;
}

__CONSTEXPR static inline zx_time_t zx_time_sub_duration(zx_time_t time, zx_duration_t duration) {
    zx_time_t x = 0;
    if (unlikely(sub_overflow(time, duration, &x))) {
        return 0;
    }
    return x;
}

__CONSTEXPR static inline zx_duration_t zx_time_sub_time(zx_time_t time1, zx_time_t time2) {
    zx_duration_t x = 0;
    if (unlikely(sub_overflow(time1, time2, &x))) {
        return 0;
    }
    return x;
}

__CONSTEXPR static inline zx_duration_t zx_duration_add_duration(zx_duration_t dur1,
                                                                 zx_duration_t dur2) {
    zx_duration_t x = 0;
    if (unlikely(add_overflow(dur1, dur2, &x))) {
        return ZX_TIME_INFINITE;
    }
    return x;
}

__CONSTEXPR static inline zx_duration_t zx_duration_sub_duration(zx_duration_t dur1,
                                                                 zx_duration_t dur2) {
    zx_duration_t x = 0;
    if (unlikely(sub_overflow(dur1, dur2, &x))) {
        return 0;
    }
    return x;
}

__CONSTEXPR static inline zx_duration_t zx_duration_mul_uint64(zx_duration_t duration,
                                                               uint64_t multiplier) {
    zx_duration_t x = 0;
    if (unlikely(mul_overflow(duration, multiplier, &x))) {
        return ZX_TIME_INFINITE;
    }
    return x;
}
