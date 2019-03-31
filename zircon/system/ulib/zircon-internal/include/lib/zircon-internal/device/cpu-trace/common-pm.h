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

// Internal abstraction of event ids.
using PmuEventId = uint16_t;

// Values for the event flags field.
constexpr uint32_t kPmuConfigFlagMask = 0x1f;
// Collect os data.
constexpr uint32_t kPmuConfigFlagOs = 1 << 0;
// Collect userspace data.
constexpr uint32_t kPmuConfigFlagUser = 1 << 1;
// Collect aspace+pc values.
// Cannot be set with timebase0 unless this is event 0 (the timebase counter).
constexpr uint32_t kPmuConfigFlagPc = 1 << 2;
// If set in |events[0].flags| then use event 0 as the timebase: data for this
// event is collected when data for event #0 is collected, and the record
// emitted for this event is either a PERFMON_RECORD_COUNT or
// PERFMON_RECORD_VALUE record (depending on what the event is).
// It is an error to have this bit set in any event other than event zero.
// It is an error to have this bit set and have events #0 rate be zero.
constexpr uint32_t kPmuConfigFlagTimebase0 = 1 << 3;
// Collect the available set of last branches.
// Branch data is emitted as PERFMON_RECORD_LAST_BRANCH records.
// This is only available when the underlying system supports it.
// Cannot be set with timebase0 unless this is event 0 (the timebase counter).
// TODO(dje): Provide knob to specify how many branches.
constexpr uint32_t kPmuConfigFlagLastBranch = 1 << 4;

} // namespace perfmon

#endif // __cplusplus

// This is for passing buffer specs to the kernel.
typedef struct {
    zx_handle_t vmo;
} zx_pmu_buffer_t;

#endif  // LIB_ZIRCON_INTERNAL_DEVICE_CPU_TRACE_COMMON_PM_H_
