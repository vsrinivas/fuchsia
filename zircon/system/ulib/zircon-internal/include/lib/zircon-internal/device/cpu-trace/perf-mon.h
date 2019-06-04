// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include <zircon/types.h>

#ifdef __Fuchsia__
#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>
#endif

namespace perfmon {

// API version number (useful when doing incompatible upgrades)
static constexpr uint16_t kApiVersion = 3;

// Buffer format version
static constexpr uint16_t kBufferVersion = 0;

// The maximum number of events we support simultaneously.
// Typically the h/w supports less than this, e.g., 7 or so.
// TODO(dje): Have the device driver multiplex the events when more is
// asked for than the h/w supports.
static constexpr uint32_t kMaxNumEvents = 32;

// Values for the |BufferHeader.arch| field.
static constexpr uint16_t kArchUnknown = 0;
static constexpr uint16_t kArchX64 = 1;
static constexpr uint16_t kArchArm64 = 2;

// Header for each data buffer.
struct BufferHeader {
    // Values for the |flags| field.
    // The buffer filled, and records were dropped.
    static constexpr uint32_t kBufferFlagFull = 1 << 0;

    // Format version number |kBufferVersion|.
    uint16_t version;

    // The architecture that generated the data.
    uint16_t arch;

    uint32_t flags;

    // zx_ticks_per_second in the kernel
    zx_ticks_t ticks_per_second;

    // Offset into the buffer of the end of the data.
    uint64_t capture_end;
};

using RecordType = uint8_t;

// Signals an invalid record type.
static constexpr RecordType kRecordTypeInvalid = 0;
// The current time, in a |TimeRecord|, to be applied to all
// subsequent records until the next time record.
static constexpr RecordType kRecordTypeTime = 1;
// The record is a |TickRecord|.
// TODO(dje): Rename? The name is confusing with time records.
static constexpr RecordType kRecordTypeTick = 2;
// The record is a |CountRecord|.
static constexpr RecordType kRecordTypeCount = 3;
// The record is a |ValueRecord|.
static constexpr RecordType kRecordTypeValue = 4;
// The record is a |PcRecord|.
static constexpr RecordType kRecordTypePc = 5;
// The record is a |LastBranchRecord|.
static constexpr RecordType kRecordTypeLastBranch = 6;

// Trace buffer space is expensive, we want to keep records small.
// Having more than 64K different events for any one arch is unlikely
// so we use 16 bits for the event id.
// To help each arch manage the plethora of different events, the event id
// is split it two parts: 5 bit event group, and 11 bit event within that
// group.
using EventId = uint16_t;

// Event id zero is reserved to mean "no event".
static constexpr EventId kEventIdNone = 0;

using EventIdGroupType = uint16_t;
using EventIdEventType = uint16_t;

static constexpr EventIdGroupType kMaxGroup = 0x1f;
static constexpr EventIdEventType kMaxEvent = 0x7ff;

// Possible values for the |group| field of |EventId|.
// TODO(dje): Reorganize these into something like
// {arch,model} -x- {fixed,programmable}, which these currently are,
// it's just not immediately apparent.
constexpr uint16_t kGroupReserved = 0;
constexpr uint16_t kGroupArch = 1;
constexpr uint16_t kGroupFixed = 2;
constexpr uint16_t kGroupModel = 3;
constexpr uint16_t kGroupMisc = 4;

static inline constexpr EventId MakeEventId(EventIdGroupType group,
                                            EventIdEventType event) {
    return static_cast<EventId>((group << 11) | event);
}

static inline constexpr EventIdGroupType GetEventIdGroup(EventId id) {
    EventIdGroupType mask = kMaxGroup;
    return static_cast<EventIdGroupType>((id >> 11) & mask);
}

static inline constexpr EventIdEventType GetEventIdEvent(EventId id) {
    EventIdEventType mask = kMaxEvent;
    return static_cast<EventIdEventType>(id & mask);
}

// The rate at which to collect data.
// For counters this is every N ticks of the counter.
using EventRate = uint32_t;

// The typical record is a tick record which is 4 + 8 bytes.
// Aligning records to 8-byte boundaries would waste a lot of space,
// so currently we align everything to 4-byte boundaries.
// TODO(dje): Collect data to see what this saves. Keep it?
#define PERFMON_ALIGN_RECORD __PACKED __ALIGNED(4)

// Trace record header.
// Note: Avoid holes in all trace records.
struct RecordHeader {
    // One of |kRecordType*|.
    uint8_t type;

    // A possible usage of this field is to add some type-specific flags.
    uint8_t reserved_flags;

    // The event the record is for.
    // If there is none then use |kEventIdNone|.
    perfmon::EventId event;
} PERFMON_ALIGN_RECORD;

// Verify our alignment assumptions.
static_assert(sizeof(RecordHeader) == 4,
              "record header not 4 bytes");

// Record the current time of the trace.
// If the event id is non-zero (!NONE) then it must be for a counting event
// and then this record is also a "tick" record indicating the counter has
// reached its sample rate. The counter resets to zero after this record.
struct TimeRecord {
    RecordHeader header;
    // The value is architecture and possibly platform specific.
    // The |ticks_per_second| field in the buffer header provides the
    // conversion factor from this value to ticks per second.
    // For x86 this is the TSC value.
    zx_ticks_t time;
} PERFMON_ALIGN_RECORD;

// Verify our alignment assumptions.
// We don't need to do this for every record, but doing it for this one
// verifies PERFMON_ALIGN_RECORD is working.
static_assert(sizeof(TimeRecord) == 12,
              "time record not 12 bytes");
static_assert(alignof(TimeRecord) == 4,
              "time record not 4-byte aligned");

// Record that a counting event reached its sample rate.
// It is expected that this record follows a TIME record.
// The counter resets to zero after this record.
// This does not include the event's value in order to keep the size small:
// the value is the sample rate which is known from the configuration.
struct TickRecord {
    RecordHeader header;
} PERFMON_ALIGN_RECORD;

// Record the value of a counter at a particular time.
// It is expected that this record follows a TIME record.
// The counter resets to zero after this record.
// This is used when another timebase is driving the sampling, e.g., another
// counter. Otherwise the "tick" record is generally used as it takes less
// space.
struct CountRecord {
    RecordHeader header;
    uint64_t count;
} PERFMON_ALIGN_RECORD;

// Record the value of an event.
// It is expected that this record follows a TIME record.
// This value is not a count and cannot be used to produce a "rate"
// (e.g., some value per second).
struct ValueRecord {
    RecordHeader header;
    uint64_t value;
} PERFMON_ALIGN_RECORD;

// Record the aspace+pc values.
// If the event id is not NONE, then this record also indicates that the
// event reached its tick point, and is used instead of a tick record. This
// record is overloaded to save space in trace buffer output.
// It is expected that this record follows a TIME record.
// This is used when doing gprof-like profiling.
// The event's value is not included here as this is typically used when
// the counter is its own trigger: the value is known from the sample rate.
struct PcRecord {
    RecordHeader header;
    // The aspace id at the time data was collected.
    // The meaning of the value is architecture-specific.
    // In the case of x86 this is the cr3 value.
    uint64_t aspace;
    uint64_t pc;
} PERFMON_ALIGN_RECORD;

// Entry in a last branch record.
struct LastBranchEntry {
    uint64_t from;
    uint64_t to;
    // Various bits of info about this branch. See |kLastBranchInfo*|.
    uint64_t info;
} PERFMON_ALIGN_RECORD;

// Utility to compute masks for fields in this file.
static inline constexpr uint64_t GenMask64(size_t len, size_t shift) {
    return ((1ull << len) - 1) << shift;
}

// Fields in |LastBranchEntry.info|.

// Number of cycles since the last branch, or zero if unknown.
// The unit of measurement is architecture-specific.
static constexpr size_t kLastBranchInfoCyclesShift = 0;
static constexpr size_t kLastBranchInfoCyclesLen = 16;
static constexpr uint64_t kLastBranchInfoCyclesMask =
    GenMask64(kLastBranchInfoCyclesShift, kLastBranchInfoCyclesLen);

// Non-zero if branch was mispredicted.
// Whether this bit is available is architecture-specific.
static constexpr size_t kLastBranchInfoMispredShift = 16;
static constexpr size_t kLastBranchInfoMispredLen = 1;
static constexpr uint64_t kLastBranchInfoMispredMask =
    GenMask64(kLastBranchInfoMispredShift, kLastBranchInfoMispredLen);

// Record a set of last branches executed.
// It is expected that this record follows a TIME record.
// Note that this record is variable-length.
// This is used when doing gprof-like profiling.
struct LastBranchRecord {
   // 32 is the max value for Skylake.
   static constexpr uint32_t kMaxNumLastBranch = 32;

    RecordHeader header;
    // Number of entries in |branch|.
    uint32_t num_branches;
    // The aspace id at the time data was collected. This is not necessarily
    // the aspace id of each branch. S/W will need to determine from the
    // branch addresses how far back aspace is valid.
    // The meaning of the value is architecture-specific.
    // In the case of x86 this is the cr3 value.
    uint64_t aspace;
    // The set of last branches, in reverse chronological order:
    // The first entry is the most recent one.
    // Note that the emitted record may be smaller than this, as indicated by
    // |num_branches|.
    // Reverse order seems most useful.
    LastBranchEntry branches[kMaxNumLastBranch];
} PERFMON_ALIGN_RECORD;

// Return the size of valid last branch record |lbr|.
static inline constexpr size_t LastBranchRecordSize(
        const LastBranchRecord* lbr) {
    return (sizeof(LastBranchRecord) -
            (LastBranchRecord::kMaxNumLastBranch -
             (lbr)->num_branches) * sizeof((lbr)->branches[0]));
}

}  // namespace perfmon
