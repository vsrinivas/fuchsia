// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains internal definitions common to all arches.
// These definitions are used for communication between the cpu-trace
// device driver and the kernel only.

#ifndef LIB_ZIRCON_INTERNAL_DEVICE_CPU_TRACE_COMMON_PM_H_
#define LIB_ZIRCON_INTERNAL_DEVICE_CPU_TRACE_COMMON_PM_H_

namespace perfmon {

// H/W properties used by common code.
struct PmuCommonProperties {
    // The H/W Performance Monitor version.
    uint16_t pm_version;

    // The maximum number of fixed events that can be simultaneously
    // supported, and their maximum width.
    uint16_t max_num_fixed_events;
    uint16_t max_fixed_counter_width;

    // The maximum number of programmable events that can be simultaneously
    // supported, and their maximum width.
    uint16_t max_num_programmable_events;
    uint16_t max_programmable_counter_width;

    // The maximum number of misc events that can be simultaneously
    // supported, and their maximum width.
    uint16_t max_num_misc_events;
    uint16_t max_misc_counter_width;
};

// Internal abstraction of event ids.
using PmuEventId = uint16_t;

// Values for the event flags field.
constexpr uint32_t kPmuConfigFlagMask = 0x1f;

// Collect os data.
constexpr uint32_t kPmuConfigFlagOs = 1 << 0;

// Collect userspace data.
constexpr uint32_t kPmuConfigFlagUser = 1 << 1;

// Collect aspace+pc values.
constexpr uint32_t kPmuConfigFlagPc = 1 << 2;

// If set then use the timebase event to drive data collection: data for this
// event is collected when data for the timebase event is collected, and the
// record emitted for this event is either a |kRecordTypeCount| or
// |kRecordTypeValue| record (depending on what the event is).
constexpr uint32_t kPmuConfigFlagUsesTimebase = 1 << 3;

// Collect the available set of last branches.
// Branch data is emitted as |kRecordTypeLastBranch| records.
// This is only available when the underlying system supports it.
// TODO(dje): Provide knob to specify how many branches.
constexpr uint32_t kPmuConfigFlagLastBranch = 1 << 4;

} // namespace perfmon

// This is for passing buffer specs to the kernel.
typedef struct {
    zx_handle_t vmo;
} zx_pmu_buffer_t;

#endif  // LIB_ZIRCON_INTERNAL_DEVICE_CPU_TRACE_COMMON_PM_H_
