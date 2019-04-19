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

// API version number (useful when doing incompatible upgrades)
#define PERFMON_API_VERSION 3

// Buffer format version
#define PERFMON_BUFFER_VERSION 0

// The maximum number of events we support simultaneously.
// Typically the h/w supports less than this, e.g., 7 or so.
// TODO(dje): Have the device driver multiplex the events when more is
// asked for than the h/w supports.
#define PERFMON_MAX_EVENTS 32u

// Header for each data buffer.
typedef struct {
    // Format version number (PERFMON_BUFFER_VERSION).
    uint16_t version;

    // The architecture that generated the data.
    uint16_t arch;
#define PERFMON_BUFFER_ARCH_UNKNOWN 0u
#define PERFMON_BUFFER_ARCH_X86_64  1u
#define PERFMON_BUFFER_ARCH_ARM64   2u

    uint32_t flags;
// The buffer filled, and records were dropped.
#define PERFMON_BUFFER_FLAG_FULL (1u << 0)

    // zx_ticks_per_second in the kernel
    zx_ticks_t ticks_per_second;

    // Offset into the buffer of the end of the data.
    uint64_t capture_end;
} perfmon_buffer_header_t;

// The various types of emitted records.
typedef enum {
  // Reserved, unused.
  PERFMON_RECORD_RESERVED = 0,
  // The current time, in a |perfmon_time_record_t|, to be applied to all
  // subsequent records until the next TIME record.
  PERFMON_RECORD_TIME = 1,
  // The record is a |perfmon_tick_record_t|.
  // TODO(dje): Rename, the name is confusing with TIME records.
  PERFMON_RECORD_TICK = 2,
  // The record is a |perfmon_count_record_t|.
  PERFMON_RECORD_COUNT = 3,
  // The record is a |perfmon_value_record_t|.
  PERFMON_RECORD_VALUE = 4,
  // The record is a |perfmon_pc_record_t|.
  PERFMON_RECORD_PC = 5,
  // The record is a |perfmon_last_branch_record_t|.
  PERFMON_RECORD_LAST_BRANCH = 6,
} perfmon_record_type_t;

// Trace buffer space is expensive, we want to keep records small.
// Having more than 64K different events for any one arch is unlikely
// so we use 16 bits for the event id.
// To help each arch manage the plethora of different events, the event id
// is split it two parts: 6 bit event group, and 10 bit event within that
// group.
// An event id of zero is defined to be unused. To simplify things we just
// take the whole set of |group| == 0 as reserved.
typedef uint16_t perfmon_event_id_t;
#define PERFMON_MAKE_EVENT_ID(group, event) (((group) << 10) | (event))
#define PERFMON_EVENT_ID_GROUP(id) (((id) >> 10) & 0x3f)
#define PERFMON_EVENT_ID_EVENT(id) ((id) & 0x3ff)
#define PERFMON_MAX_GROUP 0x3f
#define PERFMON_MAX_EVENT 0x3ff
#define PERFMON_EVENT_ID_NONE 0

// Possible values for the |group| field of |perfmon_event_id_t|.
// TODO(dje): Reorganize these into something like
// {arch,model} -x- {fixed,programmable}, which these currently are,
// it's just not immediately apparent.
typedef enum {
    PERFMON_GROUP_RESERVED = 0,
    PERFMON_GROUP_ARCH = 1,
    PERFMON_GROUP_FIXED = 2,
    PERFMON_GROUP_MODEL = 3,
    PERFMON_GROUP_MISC = 4,
} perfmon_group_type_t;

// The typical record is a tick record which is 4 + 8 bytes.
// Aligning records to 8-byte boundaries would waste a lot of space,
// so currently we align everything to 4-byte boundaries.
// TODO(dje): Collect data to see what this saves. Keep it?
#define PERFMON_ALIGN_RECORD __PACKED __ALIGNED(4)

// Trace record header.
// Note: Avoid holes in all trace records.
typedef struct {
    // One of PERFMON_RECORD_*.
    uint8_t type;

    // A possible usage of this field is to add some type-specific flags.
    uint8_t reserved_flags;

    // The event the record is for.
    // If there is none then use PERFMON_EVENT_ID_NONE.
    perfmon_event_id_t event;
} PERFMON_ALIGN_RECORD perfmon_record_header_t;

// Verify our alignment assumptions.
static_assert(sizeof(perfmon_record_header_t) == 4,
              "record header not 4 bytes");

// Record the current time of the trace.
// If the event id is non-zero (!NONE) then it must be for a counting event
// and then this record is also a "tick" record indicating the counter has
// reached its sample rate. The counter resets to zero after this record.
typedef struct {
    perfmon_record_header_t header;
    // The value is architecture and possibly platform specific.
    // The |ticks_per_second| field in the buffer header provides the
    // conversion factor from this value to ticks per second.
    // For x86 this is the TSC value.
    zx_ticks_t time;
} PERFMON_ALIGN_RECORD perfmon_time_record_t;

// Verify our alignment assumptions.
// We don't need to do this for every record, but doing it for this one
// verifies PERFMON_ALIGN_RECORD is working.
static_assert(sizeof(perfmon_time_record_t) == 12,
              "time record not 12 bytes");
static_assert(alignof(perfmon_time_record_t) == 4,
              "time record not 4-byte aligned");

// Record that a counting event reached its sample rate.
// It is expected that this record follows a TIME record.
// The counter resets to zero after this record.
// This does not include the event's value in order to keep the size small:
// the value is the sample rate which is known from the configuration.
typedef struct {
    perfmon_record_header_t header;
} PERFMON_ALIGN_RECORD perfmon_tick_record_t;

// Record the value of a counter at a particular time.
// It is expected that this record follows a TIME record.
// The counter resets to zero after this record.
// This is used when another timebase is driving the sampling, e.g., another
// counter. Otherwise the "tick" record is generally used as it takes less
// space.
typedef struct {
    perfmon_record_header_t header;
    uint64_t count;
} PERFMON_ALIGN_RECORD perfmon_count_record_t;

// Record the value of an event.
// It is expected that this record follows a TIME record.
// This value is not a count and cannot be used to produce a "rate"
// (e.g., some value per second).
typedef struct {
    perfmon_record_header_t header;
    uint64_t value;
} PERFMON_ALIGN_RECORD perfmon_value_record_t;

// Record the aspace+pc values.
// If the event id is not NONE, then this record also indicates that the
// event reached its tick point, and is used instead of a tick record. This
// record is overloaded to save space in trace buffer output.
// It is expected that this record follows a TIME record.
// This is used when doing gprof-like profiling.
// The event's value is not included here as this is typically used when
// the counter is its own trigger: the value is known from the sample rate.
typedef struct {
    perfmon_record_header_t header;
    // The aspace id at the time data was collected.
    // The meaning of the value is architecture-specific.
    // In the case of x86 this is the cr3 value.
    uint64_t aspace;
    uint64_t pc;
} PERFMON_ALIGN_RECORD perfmon_pc_record_t;

// Entry in a last branch record.
typedef struct  {
    uint64_t from;
    uint64_t to;
    // Various bits of info about this branch. See PERFMON_LAST_BRANCH_INFO_*.
    uint64_t info;
} PERFMON_ALIGN_RECORD perfmon_last_branch_t;

// Utility to compute masks for fields in this file.
#define PERFMON_GEN_MASK64(len, shift) (((1ULL << (len)) - 1) << (shift))

// Fields in |perfmon_last_branch_t.info|.

// Number of cycles since the last branch, or zero if unknown.
// The unit of measurement is architecture-specific.
#define PERFMON_LAST_BRANCH_INFO_CYCLES_SHIFT (0u)
#define PERFMON_LAST_BRANCH_INFO_CYCLES_LEN   (16u)
#define PERFMON_LAST_BRANCH_INFO_CYCLES_MASK  \
    PERFMON_GEN_MASK64(PERFMON_LAST_BRANCH_INFO_CYCLES_SHIFT, \
                       PERFMON_LAST_BRANCH_INFO_CYCLES_LEN)

// Non-zero if branch was mispredicted.
// Whether this bit is available is architecture-specific.
#define PERFMON_LAST_BRANCH_INFO_MISPRED_SHIFT (16u)
#define PERFMON_LAST_BRANCH_INFO_MISPRED_LEN   (1u)
#define PERFMON_LAST_BRANCH_INFO_MISPRED_MASK  \
    PERFMON_GEN_MASK64(PERFMON_LAST_BRANCH_INFO_MISPRED_SHIFT, \
                       PERFMON_LAST_BRANCH_INFO_MISPRED_LEN)

// Record a set of last branches executed.
// It is expected that this record follows a TIME record.
// Note that this record is variable-length.
// This is used when doing gprof-like profiling.
typedef struct {
    perfmon_record_header_t header;
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
// 32 is the max value for Skylake
#define PERFMON_MAX_NUM_LAST_BRANCH (32u)
    perfmon_last_branch_t branches[PERFMON_MAX_NUM_LAST_BRANCH];
} PERFMON_ALIGN_RECORD perfmon_last_branch_record_t;

// Return the size of valid last branch record |lbr|.
#define PERFMON_LAST_BRANCH_RECORD_SIZE(lbr) \
    (sizeof(perfmon_last_branch_record_t) - \
     (PERFMON_MAX_NUM_LAST_BRANCH - (lbr)->num_branches) * sizeof((lbr)->branches[0]))

// The properties of this system.
typedef struct {
    // S/W API version = PERFMON_API_VERSION.
    uint16_t api_version;

    // The H/W Performance Monitor version.
    uint16_t pm_version;

    // The maximum number of events that can be simultaneously supported.
    // The combination of events that can be simultaneously supported is
    // architecture/model specific.
    uint16_t max_num_events;

    // Padding/reserved.
    uint16_t reserved;

    // The maximum number of fixed events that can be simultaneously
    // supported, and their maximum width.
    // These values are for informational/display purposes.
    uint16_t max_num_fixed_events;
    uint16_t max_fixed_counter_width;

    // The maximum number of programmable events that can be simultaneously
    // supported, and their maximum width.
    // These values are for informational/display purposes.
    uint16_t max_num_programmable_events;
    uint16_t max_programmable_counter_width;

    // The maximum number of misc events that can be simultaneously
    // supported, and their maximum width.
    // These values are for informational/display purposes.
    uint16_t max_num_misc_events;
    uint16_t max_misc_counter_width;

    // Various flags.
    uint32_t flags;
#define PERFMON_PROPERTY_FLAG_HAS_LAST_BRANCH (1u << 0)
} perfmon_ioctl_properties_t;

// The type of the |rate| field of perfmon_config_t.
typedef uint32_t perfmon_rate_t;

// Passed to STAGE_CONFIG to select the data to be collected.
// Events must be consecutively allocated from the front with no holes.
// A value of PERFMON_EVENT_ID_NONE in |events| marks the end.
typedef struct {
    // Events to collect data for.
    // The values are architecture specific ids: perfmon_<arch>_event_id_t
    // Each event may appear at most once.
    // |events[0]| is special: It is used as the timebase when any other
    // event has PERFMON_CONFIG_FLAG_TIMEBASE0 set.
    perfmon_event_id_t events[PERFMON_MAX_EVENTS];

    // Sampling rate for each event in |events|.
    // If zero then do simple counting (collect a tally of the count and
    // report at the end). Otherwise (non-zero) then when the event gets
    // this many hits data is collected (e.g., pc, time).
    // The value can be non-zero only for counting based events.
    // This value is ignored if PERFMON_CONFIG_FLAG_TIMEBASE0 is set.
    // Setting PERFMON_CONFIG_FLAG_TIMEBASE0 in |flags[0]| is redundant but ok.
    perfmon_rate_t rate[PERFMON_MAX_EVENTS];

    // Flags for each event in |events|.
    // TODO(dje): hypervisor, host/guest os/user
    uint32_t flags[PERFMON_MAX_EVENTS];
// Valid bits in |flags|.
#define PERFMON_CONFIG_FLAG_MASK      0x1f
// Collect os data.
#define PERFMON_CONFIG_FLAG_OS        (1u << 0)
// Collect userspace data.
#define PERFMON_CONFIG_FLAG_USER      (1u << 1)
// Collect aspace+pc values.
#define PERFMON_CONFIG_FLAG_PC        (1u << 2)
// If set then use |events[0]| as the timebase: data for this event is
// collected when data for |events[0]| is collected, and the record emitted
// for this event is either a PERFMON_RECORD_COUNT or PERFMON_RECORD_VALUE
// record (depending on what the event is).
// It is an error to have this bit set for an event and have rate[0] be zero.
#define PERFMON_CONFIG_FLAG_TIMEBASE0 (1u << 3)
// Collect the available set of last branches.
// Branch data is emitted as PERFMON_RECORD_LAST_BRANCH records.
// This is only available when the underlying system supports it.
// TODO(dje): Provide knob to specify how many branches.
#define PERFMON_CONFIG_FLAG_LAST_BRANCH (1u << 4)
} perfmon_config_t;

///////////////////////////////////////////////////////////////////////////////

#ifdef __Fuchsia__

// ioctls

// Fetch the cpu trace properties of the system.
// Output: perfmon_ioctl_properties_t
#define IOCTL_PERFMON_GET_PROPERTIES \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_PERFMON, 0)
IOCTL_WRAPPER_OUT(ioctl_perfmon_get_properties,
                  IOCTL_PERFMON_GET_PROPERTIES,
                  perfmon_ioctl_properties_t)

// The allocation configuration for a data collection run.
// This is generally the first call to allocate resources for a trace,
// "trace" is used generically here: == "data collection run".
typedef struct {
    // must be #cpus for now
    uint32_t num_buffers;

    // each cpu gets same buffer size
    uint32_t buffer_size_in_pages;
} ioctl_perfmon_alloc_t;

// Create a trace, allocating the needed trace buffers and other resources.
// "other resources" is basically a catch-all for other things that will
// be needed. This does not include reserving the events, that is done later
// by STAGE_CONFIG.
// Input: ioctl_perfmon_alloc_t
#define IOCTL_PERFMON_ALLOC_TRACE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_PERFMON, 1)
IOCTL_WRAPPER_IN(ioctl_perfmon_alloc_trace, IOCTL_PERFMON_ALLOC_TRACE,
                 ioctl_perfmon_alloc_t)

// Free all trace buffers and any other resources allocated for the trace.
// This is also done when the fd is closed (as well as stopping the trace).
#define IOCTL_PERFMON_FREE_TRACE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_PERFMON, 2)
IOCTL_WRAPPER(ioctl_perfmon_free_trace, IOCTL_PERFMON_FREE_TRACE)

// Return trace allocation config.
// Output: ioctl_perfmon_alloc_t
#define IOCTL_PERFMON_GET_ALLOC \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_PERFMON, 3)
IOCTL_WRAPPER_OUT(ioctl_perfmon_get_alloc, IOCTL_PERFMON_GET_ALLOC,
                  ioctl_perfmon_alloc_t)

// Stage performance monitor specification for a cpu.
// Must be called with data collection off and after ALLOC.
// Note: This doesn't actually configure the h/w, this just stages
// the values for subsequent use by START.
// Input: perfmon_config_t
#define IOCTL_PERFMON_STAGE_CONFIG \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_PERFMON, 4)
IOCTL_WRAPPER_IN(ioctl_perfmon_stage_config, IOCTL_PERFMON_STAGE_CONFIG,
                 perfmon_config_t)

// Fetch performance monitor specification for a cpu.
// Must be called with data collection off and after STAGE_CONFIG.
// Output: perfmon_config_t
#define IOCTL_PERFMON_GET_CONFIG \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_PERFMON, 5)
IOCTL_WRAPPER_OUT(ioctl_perfmon_get_config, IOCTL_PERFMON_GET_CONFIG,
                  perfmon_config_t)

typedef struct {
    uint32_t descriptor;
} ioctl_perfmon_buffer_handle_req_t;

// Return a handle of a trace buffer.
// Input: trace buffer descriptor (0, 1, 2, ..., |num_buffers|-1)
// Output: handle of the vmo of the buffer
#define IOCTL_PERFMON_GET_BUFFER_HANDLE \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_PERFMON, 6)
IOCTL_WRAPPER_INOUT(ioctl_perfmon_get_buffer_handle,
                    IOCTL_PERFMON_GET_BUFFER_HANDLE,
                    ioctl_perfmon_buffer_handle_req_t, zx_handle_t)

// Turn on data collection.
// Must be called after ALLOC+STAGE_CONFIG and with data collection off.
#define IOCTL_PERFMON_START \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_PERFMON, 7)
IOCTL_WRAPPER(ioctl_perfmon_start, IOCTL_PERFMON_START)

// Turn off data collection.
// May be called any time after ALLOC has been called and before FREE.
// May be called multiple times.
#define IOCTL_PERFMON_STOP \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_PERFMON, 8)
IOCTL_WRAPPER(ioctl_perfmon_stop, IOCTL_PERFMON_STOP)

#endif // __Fuchsia__
