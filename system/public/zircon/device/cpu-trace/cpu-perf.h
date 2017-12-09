// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <stdint.h>

#ifdef __Fuchsia__
#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/types.h>
#include <stddef.h>
#endif

__BEGIN_CDECLS

// API version number (useful when doing incompatible upgrades)
#define CPUPERF_API_VERSION 3

// Buffer format version
#define CPUPERF_BUFFER_VERSION 0

// The maximum number of events we support simultaneously.
// Typically the h/w supports less than this, e.g., 7 or so.
// TODO(dje): Have the device driver multiplex the events when more is
// asked for than the h/w supports.
#define CPUPERF_MAX_EVENTS 32

// Header for each data buffer.
typedef struct {
    // Format version number (CPUPERF_BUFFER_VERSION).
    uint16_t version;

    // The architecture that generated the data.
    uint16_t arch;
#define CPUPERF_BUFFER_ARCH_UNKNOWN 0
#define CPUPERF_BUFFER_ARCH_X86_64  1
#define CPUPERF_BUFFER_ARCH_ARM64   2

    // There are no flags yet, but reserve a spot.
    uint32_t flags;
// The buffer filled, and records were dropped.
#define CPUPERF_BUFFER_FLAG_FULL (1ul << 0)

    // zx_ticks_per_second in the kernel
    uint64_t ticks_per_second;

    // Offset into the buffer of the end of the data.
    uint64_t capture_end;
} __PACKED cpuperf_buffer_header_t;

// The type of "sampling mode" record.
typedef enum {
  // Reserved, unused.
  CPUPERF_RECORD_RESERVED = 0,
  // The record is a |cpuperf_tick_record_t|.
  CPUPERF_RECORD_TICK = 1,
  // The record is a |cpuperf_count_record_t|.
  CPUPERF_RECORD_COUNT = 2,
  // The record is a |cpuperf_value_record_t|.
  CPUPERF_RECORD_VALUE = 3,
  // The record is a |cpuperf_pc_record_t|.
  CPUPERF_RECORD_PC = 4,
  // non-ABI
  CPUPERF_NUM_RECORD_TYPES = 5,
} cpuperf_record_type_t;

// Trace buffer space is expensive, we want to keep records small.
// Having more than 64K different events for any one arch is unlikely
// so we use 16 bits for the event id.
// To help each arch manage the plethora of different events, the event id
// is split it two parts: 6 bit event unit, and 10 bit event within that unit.
// An event id of zero is defined to be unused. To simplify things we just
// take the whole set of |unit| == 0 as reserved.
typedef uint16_t cpuperf_event_id_t;
#define CPUPERF_MAKE_EVENT_ID(unit, event) (((unit) << 10) | (event))
#define CPUPERF_EVENT_ID_UNIT(id) (((id) >> 10) & 0x3f)
#define CPUPERF_EVENT_ID_EVENT(id) ((id) & 0x3ff)
#define CPUPERF_MAX_UNIT 0x3f
#define CPUPERF_MAX_EVENT 0x3ff
#define CPUPERF_EVENT_ID_NONE 0

// Possible values for the |unit| field of |cpuperf_event_id_t|.
typedef enum {
    CPUPERF_UNIT_RESERVED = 0,
    CPUPERF_UNIT_ARCH = 1,
    CPUPERF_UNIT_FIXED = 2,
    CPUPERF_UNIT_MODEL = 3,
    CPUPERF_UNIT_MISC = 4,
} cpuperf_unit_type_t;

// Sampling mode data header.
// Note: Avoid holes in trace records.
typedef struct {
    // One of CPUPERF_RECORD_*.
    uint8_t type;

    // A possible usage of this field is to add some type-specific flags.
    uint8_t reserved_flags;

    // The event the record is for.
    cpuperf_event_id_t event;

    // TODO(dje): Remove when |time| becomes 32 bits.
    uint32_t reserved;

    // TODO(dje): Reduce this to 32 bits (e.g., by adding clock records to
    // the buffer).
    zx_time_t time;
} __PACKED cpuperf_record_header_t;

static_assert(sizeof(cpuperf_record_header_t) % 8 == 0,
              "record header not multiple of 64 bits");

// Record the time an event was sampled.
// This does not include the event value in order to keep the size small.
// This can only be used for values that count something.
// This is for use when the event is its own trigger so the user should know
// what value was: it's the sample rate.
typedef struct {
    cpuperf_record_header_t header;
} __PACKED cpuperf_tick_record_t;

// Record the value of a counter at a particular time, with the count starting
// from the last occurrence of this record (the counter is effectively reset
// to zero when the record is emitted).
// This is used when another timebase is driving the sampling, e.g., another
// counter. Otherwise the "tick" record is generally used as it takes less
// space.
typedef struct {
    cpuperf_record_header_t header;
    uint64_t count;
} __PACKED cpuperf_count_record_t;

// Record the value of an event at a particular time.
// This value is not a count and cannot be used to produce a "rate"
// (e.g., some value per second).
// This is used when another timebase is driving the sampling, e.g., another
// counter.
typedef struct {
    cpuperf_record_header_t header;
    uint64_t value;
} __PACKED cpuperf_value_record_t;

// Record the aspace+pc values.
// This is used when doing gprof-like profiling.
// There is no point in recording the counter's value here as the counter
// must be its own trigger.
typedef struct {
    cpuperf_record_header_t header;
    // In the case of x86 this is the cr3 value.
    uint64_t aspace;
    uint64_t pc;
} __PACKED cpuperf_pc_record_t;

// The properties of this system.
typedef struct {
    // S/W API version = CPUPERF_API_VERSION.
    uint32_t api_version;
    // The H/W Performance Monitor version.
    uint32_t pm_version;
    // The number of fixed events.
    uint32_t num_fixed_events;
    // The number of programmable events.
    uint32_t num_programmable_events;
    // For fixed events that are counters, the width in bits.
    uint32_t fixed_counter_width;
    // For programmable events that are counters, the width in bits.
    uint32_t programmable_counter_width;
} cpuperf_properties_t;

// Passed to STAGE_CONFIG to select the data to be collected.
// Events must be consecutively allocated from the front with no holes.
// A value of CPUPERF_EVENT_ID_NONE in |events| marks the end.
typedef struct {
    // Events to collect data for.
    // The values are architecture specific ids: cpuperf_<arch>_event_id_t
    // Each event may appear at most once.
    // |events[0]| is special: It is used as the timebase when any other
    // event has CPUPERF_CONFIG_FLAG_TIMEBASE0 set.
    cpuperf_event_id_t events[CPUPERF_MAX_EVENTS];

    // Sampling rate for each event in |events|.
    // If zero then do simple counting (collect a tally of the count and
    // report at the end). Otherwise (non-zero) then when the event gets
    // this many hits data is collected (e.g., pc, time).
    // The value can be non-zero only for counting based events.
    // This value is ignored if CPUPERF_CONFIG_FLAG_TIMEBASE0 is set.
    // Setting CPUPERF_CONFIG_FLAG_TIMEBASE0 in |flags[0]| is redundant but ok.
    uint32_t rate[CPUPERF_MAX_EVENTS];

    // Flags for each event in |events|.
    // TODO(dje): hypervisor, host/guest os/user
    uint32_t flags[CPUPERF_MAX_EVENTS];
// Collect os data.
#define CPUPERF_CONFIG_FLAG_OS        (1u << 0)
// Collect userspace data.
#define CPUPERF_CONFIG_FLAG_USER      (1u << 1)
// Collect aspace+pc values.
#define CPUPERF_CONFIG_FLAG_PC        (1u << 2)
// If set then use |events[0]| as the timebase: data for this event is
// collected when data for |events[0]| is collected, and the record emitted
// for this event is either a CPUPERF_RECORD_COUNT or CPUPERF_RECORD_VALUE
// record (depending on what the event is).
// It is an error to have this bit set for an event and have rate[0] be zero.
#define CPUPERF_CONFIG_FLAG_TIMEBASE0 (1u << 3)
} cpuperf_config_t;

///////////////////////////////////////////////////////////////////////////////

#ifdef __Fuchsia__

// ioctls

// Fetch the cpu trace properties of the system.
// Output: cpuperf_properties_t
#define IOCTL_CPUPERF_GET_PROPERTIES \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_CPUPERF, 0)
IOCTL_WRAPPER_OUT(ioctl_cpuperf_get_properties,
                  IOCTL_CPUPERF_GET_PROPERTIES,
                  cpuperf_properties_t);

// The allocation configuration for a data collection run.
// This is generally the first call to allocate resources for a trace,
// "trace" is used generically here: == "data collection run".
typedef struct {
    // must be #cpus for now
    uint32_t num_buffers;

    // each cpu gets same buffer size
    uint32_t buffer_size;
} ioctl_cpuperf_alloc_t;

// Create a trace, allocating the needed trace buffers and other resources.
// "other resources" is basically a catch-all for other things that will
// be needed. This does not include reserving the events, that is done later
// by STAGE_CONFIG.
// Input: ioctl_cpuperf_alloc_t
#define IOCTL_CPUPERF_ALLOC_TRACE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_CPUPERF, 1)
IOCTL_WRAPPER_IN(ioctl_cpuperf_alloc_trace, IOCTL_CPUPERF_ALLOC_TRACE,
                 ioctl_cpuperf_alloc_t);

// Free all trace buffers and any other resources allocated for the trace.
// This is also done when the fd is closed (as well as stopping the trace).
#define IOCTL_CPUPERF_FREE_TRACE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_CPUPERF, 2)
IOCTL_WRAPPER(ioctl_cpuperf_free_trace, IOCTL_CPUPERF_FREE_TRACE);

// Return trace allocation config.
// Output: ioctl_cpuperf_alloc_t
#define IOCTL_CPUPERF_GET_ALLOC \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_CPUPERF, 3)
IOCTL_WRAPPER_OUT(ioctl_cpuperf_get_alloc, IOCTL_CPUPERF_GET_ALLOC,
                  ioctl_cpuperf_alloc_t);

// Stage performance monitor specification for a cpu.
// Must be called with data collection off and after ALLOC.
// Note: This doesn't actually configure the h/w, this just stages
// the values for subsequent use by START.
// Input: cpuperf_config_t
#define IOCTL_CPUPERF_STAGE_CONFIG \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_CPUPERF, 4)
IOCTL_WRAPPER_IN(ioctl_cpuperf_stage_config, IOCTL_CPUPERF_STAGE_CONFIG,
                 cpuperf_config_t);

// Fetch performance monitor specification for a cpu.
// Must be called with data collection off and after STAGE_CONFIG.
// Output: cpuperf_config_t
#define IOCTL_CPUPERF_GET_CONFIG \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_CPUPERF, 5)
IOCTL_WRAPPER_OUT(ioctl_cpuperf_get_config, IOCTL_CPUPERF_GET_CONFIG,
                  cpuperf_config_t);

typedef struct {
    uint32_t descriptor;
} ioctl_cpuperf_buffer_handle_req_t;

// Return a handle of a trace buffer.
// Input: trace buffer descriptor (0, 1, 2, ..., |num_buffers|-1)
// Output: handle of the vmo of the buffer
#define IOCTL_CPUPERF_GET_BUFFER_HANDLE \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_CPUPERF, 6)
IOCTL_WRAPPER_INOUT(ioctl_cpuperf_get_buffer_handle,
                    IOCTL_CPUPERF_GET_BUFFER_HANDLE,
                    ioctl_cpuperf_buffer_handle_req_t, zx_handle_t);

// Turn on data collection.
// Must be called after ALLOC+STAGE_CONFIG and with data collection off.
#define IOCTL_CPUPERF_START \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_CPUPERF, 7)
IOCTL_WRAPPER(ioctl_cpuperf_start, IOCTL_CPUPERF_START);

// Turn off data collection.
// May be called any time after ALLOC has been called and before FREE.
// May be called multiple times.
#define IOCTL_CPUPERF_STOP \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_CPUPERF, 8)
IOCTL_WRAPPER(ioctl_cpuperf_stop, IOCTL_CPUPERF_STOP);

#endif // __Fuchsia__

__END_CDECLS
