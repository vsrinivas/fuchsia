// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

// These functions perform overflow-safe time arithmetic, clamping to ZX_TIME_INFINITE in case of
// overflow and ZX_TIME_INFINITE_PAST in case of underflow.
//
// C++ code should use zx::time and zx::duration instead.
//
// The naming scheme is zx_<first argument>_<operation>_<second argument>.
//
// TODO(maniscalco): Consider expanding the set of operations to include division, modulo, unit
// conversion, and floating point math.

__CONSTEXPR static inline zx_time_t zx_time_add_duration(zx_time_t time, zx_duration_t duration) {
    zx_time_t x = 0;
    if (unlikely(add_overflow(time, duration, &x))) {
        if (x >= 0) {
            return ZX_TIME_INFINITE_PAST;
        } else {
            return ZX_TIME_INFINITE;
        }
    }
    return x;
}

__CONSTEXPR static inline zx_time_t zx_time_sub_duration(zx_time_t time, zx_duration_t duration) {
    zx_time_t x = 0;
    if (unlikely(sub_overflow(time, duration, &x))) {
        if (x >= 0) {
            return ZX_TIME_INFINITE_PAST;
        } else {
            return ZX_TIME_INFINITE;
        }

    }
    return x;
}

__CONSTEXPR static inline zx_duration_t zx_time_sub_time(zx_time_t time1, zx_time_t time2) {
    zx_duration_t x = 0;
    if (unlikely(sub_overflow(time1, time2, &x))) {
        if (x >= 0) {
            return ZX_TIME_INFINITE_PAST;
        } else {
            return ZX_TIME_INFINITE;
        }
    }
    return x;
}

__CONSTEXPR static inline zx_duration_t zx_duration_add_duration(zx_duration_t dur1,
                                                                 zx_duration_t dur2) {
    zx_duration_t x = 0;
    if (unlikely(add_overflow(dur1, dur2, &x))) {
        if (x >= 0) {
            return ZX_TIME_INFINITE_PAST;
        } else {
            return ZX_TIME_INFINITE;
        }
    }
    return x;
}

__CONSTEXPR static inline zx_duration_t zx_duration_sub_duration(zx_duration_t dur1,
                                                                 zx_duration_t dur2) {
    zx_duration_t x = 0;
    if (unlikely(sub_overflow(dur1, dur2, &x))) {
        if (x >= 0) {
            return ZX_TIME_INFINITE_PAST;
        } else {
            return ZX_TIME_INFINITE;
        }
    }
    return x;
}

__CONSTEXPR static inline zx_duration_t zx_duration_mul_int64(zx_duration_t duration,
                                                              int64_t multiplier) {
    zx_duration_t x = 0;
    if (unlikely(mul_overflow(duration, multiplier, &x))) {
        if ((duration > 0 && multiplier > 0) || (duration < 0 && multiplier < 0)) {
            return ZX_TIME_INFINITE;
        } else {
            return ZX_TIME_INFINITE_PAST;
        }
    }
    return x;
}
