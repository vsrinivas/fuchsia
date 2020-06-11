// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_RTC_LIB_RTC_INCLUDE_LIBRTC_H_
#define SRC_DEVICES_RTC_LIB_RTC_INCLUDE_LIBRTC_H_

#include <fuchsia/hardware/rtc/c/fidl.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

// Parse the `clock.backstop` argument, if supplied, and return the value. On
// failure, or when the argument is not set, 0 is returned.
uint64_t rtc_backstop_seconds(void);

// Basic validation that |rtc| has reasonable values. Does not check leap year.
bool rtc_is_invalid(const fuchsia_hardware_rtc_Time* rtc);

// Computes seconds (Unix epoch) to |rtc|. Does not validate. Does not handle times
// earlier than 2000/1/1T00:00:00.
uint64_t seconds_since_epoch(const fuchsia_hardware_rtc_Time* rtc);
void seconds_to_rtc(uint64_t seconds, fuchsia_hardware_rtc_Time* rtc);

// Validates and cleans the given |rtc| value. If the value is nonsensical, it
// is updated to to `clock.backstop` if that is available, otherwise the first
// of |default_year|.
void sanitize_rtc(void* ctx, fuchsia_hardware_rtc_Time* rtc,
                  zx_status_t (*rtc_get)(void*, fuchsia_hardware_rtc_Time*),
                  zx_status_t (*rtc_set)(void*, const fuchsia_hardware_rtc_Time*));

// Utility binary-coded-decimal routines.
uint8_t to_bcd(uint8_t binary);
uint8_t from_bcd(uint8_t bcd);

__END_CDECLS

#endif  // SRC_DEVICES_RTC_LIB_RTC_INCLUDE_LIBRTC_H_
