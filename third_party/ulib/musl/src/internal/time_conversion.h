#pragma once

#define __NEED_struct_timespec

#include <bits/alltypes.h>
#include <magenta/types.h>

static inline mx_time_t __timespec_to_mx_time_t(struct timespec timespec) {
    // TODO(kulakowski) Systematic time overflow checking.
    mx_time_t nanos = timespec.tv_nsec;
    nanos += timespec.tv_sec * 1000000000ull;
    return nanos;
}
