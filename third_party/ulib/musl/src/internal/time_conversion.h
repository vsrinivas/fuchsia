#pragma once

#define __NEED_struct_timespec

#include <bits/alltypes.h>
#include <zircon/types.h>

static inline zx_time_t __timespec_to_zx_time_t(struct timespec timespec) {
    // TODO(kulakowski) Systematic time overflow checking.
    zx_time_t nanos = timespec.tv_nsec;
    nanos += timespec.tv_sec * 1000000000ull;
    return nanos;
}
