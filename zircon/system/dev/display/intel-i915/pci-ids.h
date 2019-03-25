// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <zircon/assert.h>

namespace i915 {

static bool is_skl(uint16_t device_id) {
    return (device_id & 0xff00) == 0x1900;
}

static bool is_kbl(uint16_t device_id) {
    return (device_id & 0xff00) == 0x5900;
}

static bool is_skl_u(uint16_t device_id) {
    return device_id == 0x1916 || device_id == 0x1906 || device_id == 0x1926
            || device_id == 0x1927 || device_id == 0x1923;
}

static bool is_skl_y(uint16_t device_id) {
    return device_id == 0x191e;
}

static bool is_kbl_u(uint16_t device_id) {
    return device_id == 0x5916 || device_id == 0x5926 || device_id == 0x5906
            || device_id == 0x5927 || device_id == 0x3ea5;
}

static bool is_kbl_y(uint16_t device_id) {
    return device_id == 0x591e;
}

} // namespace i915
