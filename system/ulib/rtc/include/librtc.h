// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <zircon/device/rtc.h>

zx_status_t set_utc_offset(const rtc_t* rtc);
void sanitize_rtc(void* ctx, zx_protocol_device_t* dev, rtc_t* rtc);
bool rtc_is_invalid(const rtc_t* rtc);

uint8_t to_bcd(uint8_t binary);
uint8_t from_bcd(uint8_t bcd);
