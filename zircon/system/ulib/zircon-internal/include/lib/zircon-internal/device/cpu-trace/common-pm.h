// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains internal definitions common to all arches.
// These definitions are used for communication between the cpu-trace
// device driver and the kernel only.

#ifndef LIB_ZIRCON_INTERNAL_DEVICE_CPU_TRACE_COMMON_PM_H_
#define LIB_ZIRCON_INTERNAL_DEVICE_CPU_TRACE_COMMON_PM_H_

#ifdef __cplusplus

namespace perfmon {

// H/W properties used by common code.
struct PmuCommonProperties {
    // The H/W Performance Monitor version.
    uint16_t pm_version;
    // The number of fixed events.
    uint16_t num_fixed_events;
    // The number of programmable events.
    uint16_t num_programmable_events;
    // The number of misc events.
    uint16_t num_misc_events;
    // For fixed events that are counters, the width in bits.
    uint16_t fixed_counter_width;
    // For programmable events that are counters, the width in bits.
    uint16_t programmable_counter_width;
};

} // namespace perfmon

#endif // __cplusplus

// This is for passing buffer specs to the kernel.
typedef struct {
    zx_handle_t vmo;
} zx_pmu_buffer_t;

#endif  // LIB_ZIRCON_INTERNAL_DEVICE_CPU_TRACE_COMMON_PM_H_
