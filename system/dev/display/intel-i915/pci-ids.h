// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <zircon/assert.h>

namespace i915 {

static bool is_gen9(uint16_t device_id) {
    // Skylake graphics all match 0x19XX and kaby lake graphics all match 0x59xx.
    if ((device_id & 0xff00) == 0x1900) {
        return device_id == 0x191b || device_id == 0x1912 || device_id == 0x191d
                || device_id == 0x1902 || device_id == 0x1916 || device_id == 0x191e
                || device_id == 0x1906 || device_id == 0x190b || device_id == 0x1926
                || device_id == 0x1927 || device_id == 0x1923 || device_id == 0x193b
                || device_id == 0x192d || device_id == 0x193d;
    } else if ((device_id & 0xff00) == 0x5900) {
        return device_id == 0x5916 || device_id == 0x591e || device_id == 0x591b
                || device_id == 0x5912 || device_id == 0x5926 || device_id == 0x5906
                || device_id == 0x5927 || device_id == 0x5902 || device_id == 0x591a
                || device_id == 0x591d;
    }
    return false;
}

static bool is_skl(uint16_t device_id) {
    ZX_ASSERT(is_gen9(device_id));
    return (device_id & 0xff00) == 0x1900;
}

static bool is_kbl(uint16_t device_id) {
    ZX_ASSERT(is_gen9(device_id));
    return (device_id & 0xff00) == 0x5900;
}

static bool is_skl_u(uint16_t device_id) {
    ZX_ASSERT(is_gen9(device_id));
    return device_id == 0x1916 || device_id == 0x1906 || device_id == 0x1926
            || device_id == 0x1927 || device_id == 0x1923;
}

static bool is_skl_y(uint16_t device_id) {
    ZX_ASSERT(is_gen9(device_id));
    return device_id == 0x191e;
}

static bool is_kbl_u(uint16_t device_id) {
    ZX_ASSERT(is_gen9(device_id));
    return device_id == 0x5916 || device_id == 0x5926 || device_id == 0x5906 || device_id == 0x5927;
}

static bool is_kbl_y(uint16_t device_id) {
    ZX_ASSERT(is_gen9(device_id));
    return device_id == 0x591e;
}

} // namespace i915
