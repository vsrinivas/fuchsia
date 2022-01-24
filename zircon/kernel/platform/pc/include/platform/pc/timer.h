// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PLATFORM_PC_INCLUDE_PLATFORM_PC_TIMER_H_
#define ZIRCON_KERNEL_PLATFORM_PC_INCLUDE_PLATFORM_PC_TIMER_H_

#include <zircon/time.h>

zx_duration_t convert_raw_tsc_duration_to_nanoseconds(int64_t duration);
zx_time_t convert_raw_tsc_timestamp_to_clock_monotonic(int64_t ts);

#endif  // ZIRCON_KERNEL_PLATFORM_PC_INCLUDE_PLATFORM_PC_TIMER_H_
