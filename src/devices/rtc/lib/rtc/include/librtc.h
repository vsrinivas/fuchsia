// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_RTC_LIB_RTC_INCLUDE_LIBRTC_H_
#define SRC_DEVICES_RTC_LIB_RTC_INCLUDE_LIBRTC_H_

#include <stdint.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

// Utility binary-coded-decimal routines.
uint8_t to_bcd(uint8_t binary);
uint8_t from_bcd(uint8_t bcd);

__END_CDECLS

#endif  // SRC_DEVICES_RTC_LIB_RTC_INCLUDE_LIBRTC_H_
