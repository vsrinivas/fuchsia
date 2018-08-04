// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/device/rtc.h>
#include <ddk/device.h>

__BEGIN_CDECLS

// Basic validation that |rtc| has reasonable values. Does not check leap year.
bool rtc_is_invalid(const rtc_t* rtc);

// Computes seconds (Unix epoch) to |rtc|. Does not validate. Does not handle times
// earlier than 2000/1/1T00:00:00.
uint64_t seconds_since_epoch(const rtc_t* rtc);

// Validates and cleans what an RTC device |dev| returns. If the device returns
// nonsensical values, it sets |rtc| to  2018/1/1T00:00:00.
void sanitize_rtc(void* ctx, zx_protocol_device_t* dev, rtc_t* rtc);

// Utility binary-coded-decimal routines.
uint8_t to_bcd(uint8_t binary);
uint8_t from_bcd(uint8_t bcd);

__END_CDECLS
