// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <inttypes.h>

namespace edid {

typedef struct timing_params {
    uint32_t pixel_freq_10khz;

    uint32_t horizontal_addressable;
    uint32_t horizontal_front_porch;
    uint32_t horizontal_sync_pulse;
    uint32_t horizontal_blanking;

    uint32_t vertical_addressable;
    uint32_t vertical_front_porch;
    uint32_t vertical_sync_pulse;
    uint32_t vertical_blanking;

    uint32_t flags;

    static constexpr uint32_t kPositiveHsync = (1 << 1);
    static constexpr uint32_t kPositiveVsync = (1 << 0);
    static constexpr uint32_t kInterlaced = (1 << 2);
    // Flag indicating alternating vblank lengths of |vertical_blanking|
    // and |vertical_blanking| + 1. The +1 is obtained by adding .5 to
    // the vfront and vback timings.
    static constexpr uint32_t kAlternatingVblank = (1 << 3);
    static constexpr uint32_t kDoubleClocked = (1 << 4);

    uint32_t vertical_refresh_e2;
} timing_params_t;

namespace internal {

extern const timing_params_t* dmt_timings;
extern const uint32_t dmt_timings_count;

extern const timing_params_t* cea_timings;
extern const uint32_t cea_timings_count;

} // namespace internal
} // namespace edid
