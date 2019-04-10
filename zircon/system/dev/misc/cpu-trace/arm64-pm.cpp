// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// See the README.md in this directory for documentation.

#include <assert.h>

#include <ddk/debug.h>
#include <ddk/protocol/platform/device.h>

#include "perf-mon.h"

namespace perfmon {

// There's only a few fixed events, so handle them directly.
enum FixedEventId {
#define DEF_FIXED_EVENT(symbol, event_name, id, regnum, flags, readable_name, description) \
    symbol ## _ID = PERFMON_MAKE_EVENT_ID(PERFMON_GROUP_FIXED, id),
#include <lib/zircon-internal/device/cpu-trace/arm64-pm-events.inc>
};

// Verify each fixed counter regnum < ARM64_PMU_MAX_FIXED_COUNTERS.
#define DEF_FIXED_EVENT(symbol, event_name, id, regnum, flags, readable_name, description) \
    && (regnum) < ARM64_PMU_MAX_FIXED_COUNTERS
static_assert(1
#include <lib/zircon-internal/device/cpu-trace/arm64-pm-events.inc>
    , "");

enum ArchEvent {
#define DEF_ARCH_EVENT(symbol, event_name, id, pmceid_bit, event, flags, readable_name, description) \
    symbol,
#include <lib/zircon-internal/device/cpu-trace/arm64-pm-events.inc>
};

static const EventDetails kArchEvents[] = {
#define DEF_ARCH_EVENT(symbol, event_name, id, pmceid_bit, event, flags, readable_name, description) \
    { id, event, flags },
#include <lib/zircon-internal/device/cpu-trace/arm64-pm-events.inc>
};

// A table to map event id to index in |kArchEvents|.
// We use the kConstant naming style as once computed it is constant.
static const uint16_t* kArchEventMap;
static size_t kArchEventMapSize;

// Initialize the event maps.
// If there's a problem with the database just flag the error but don't crash.

static zx_status_t InitializeEventMaps() {
    zx_status_t status = BuildEventMap(kArchEvents, countof(kArchEvents),
                                       &kArchEventMap, &kArchEventMapSize);
    if (status != ZX_OK) {
        return status;
    }

    return ZX_OK;
}


// Each arch provides its own |InitOnce()| method.

zx_status_t PerfmonDevice::InitOnce() {
    zx_status_t status = GetHwProperties();
    if (status != ZX_OK) {
        return status;
    }

    // KISS and begin with pmu v3.
    // Note: This should agree with the kernel driver's check.
    if (pmu_hw_properties_.pm_version < 3) {
        zxlogf(INFO, "%s: PM version 3 or above is required\n", __func__);
        return ZX_ERR_NOT_SUPPORTED;
    }

    status = InitializeEventMaps();
    if (status != ZX_OK) {
        return status;
    }

    zxlogf(TRACE, "ARM64 Performance Monitor configuration for this chipset:\n");
    zxlogf(TRACE, "PMU: version: %u\n", pmu_hw_properties_.pm_version);
    zxlogf(TRACE, "PMU: num_programmable_events: %u\n",
           pmu_hw_properties_.num_programmable_events);
    zxlogf(TRACE, "PMU: num_fixed_events: %u\n",
           pmu_hw_properties_.num_fixed_events);
    zxlogf(TRACE, "PMU: programmable_counter_width: %u\n",
           pmu_hw_properties_.programmable_counter_width);
    zxlogf(TRACE, "PMU: fixed_counter_width: %u\n",
           pmu_hw_properties_.fixed_counter_width);

    return ZX_OK;
}


// Each arch provides its own |PmuStageConfig()| method.

struct StagingState {
    // Maximum number of each event we can handle.
    unsigned max_num_fixed;
    unsigned max_num_programmable;

    // The number of events in use.
    unsigned num_fixed;
    unsigned num_programmable;

    // The maximum value the counter can have before overflowing.
    uint64_t max_fixed_value;
    uint64_t max_programmable_value;

    bool have_timebase0_user;
};

static zx_status_t pmu_stage_fixed_config(const perfmon_config_t* icfg,
                                          StagingState* ss,
                                          unsigned input_index,
                                          Arm64PmuConfig* ocfg) {
    const unsigned ii = input_index;
    const perfmon_event_id_t id = icfg->events[ii];
    bool uses_timebase0 = !!(icfg->flags[ii] & PERFMON_CONFIG_FLAG_TIMEBASE0);

    // There's only one fixed counter on ARM64, the cycle counter.
    if (id != FIXED_CYCLE_COUNTER_ID) {
        zxlogf(ERROR, "%s: Invalid fixed event [%u]\n", __func__, ii);
        return ZX_ERR_INVALID_ARGS;
    }
    if (ss->num_fixed > 0) {
        zxlogf(ERROR, "%s: Fixed event [%u] already provided\n",
               __func__, id);
        return ZX_ERR_INVALID_ARGS;
    }
    ocfg->fixed_events[ss->num_fixed] = id;
    if ((uses_timebase0 && input_index != 0) || icfg->rate[ii] == 0) {
        ocfg->fixed_initial_value[ss->num_fixed] = 0;
    } else {
#if 0 // TODO(ZX-3302): Disable until overflow interrupts are working.
        // The cycle counter is 64 bits so there's no need to check
        // |icfg->rate[ii]| here.
        ZX_DEBUG_ASSERT(ss->max_fixed_value == UINT64_MAX);
        ocfg->fixed_initial_value[ss->num_fixed] =
            ss->max_fixed_value - icfg->rate[ii] + 1;
#else
        zxlogf(ERROR, "%s: data collection rates not supported yet\n", __func__);
        return ZX_ERR_NOT_SUPPORTED;
#endif
    }
    ocfg->fixed_flags[ss->num_fixed] = icfg->flags[ii];

    ++ss->num_fixed;
    return ZX_OK;
}

static zx_status_t pmu_stage_programmable_config(const perfmon_config_t* icfg,
                                                 StagingState* ss,
                                                 unsigned input_index,
                                                 Arm64PmuConfig* ocfg) {
    const unsigned ii = input_index;
    perfmon_event_id_t id = icfg->events[ii];
    unsigned group = PERFMON_EVENT_ID_GROUP(id);
    unsigned event = PERFMON_EVENT_ID_EVENT(id);
    bool uses_timebase0 = !!(icfg->flags[ii] & PERFMON_CONFIG_FLAG_TIMEBASE0);

    // TODO(dje): Verify no duplicates.
    if (ss->num_programmable == ss->max_num_programmable) {
        zxlogf(ERROR, "%s: Too many programmable counters provided\n",
               __func__);
        return ZX_ERR_INVALID_ARGS;
    }
    ocfg->programmable_events[ss->num_programmable] = id;
    if ((uses_timebase0 && input_index != 0) || icfg->rate[ii] == 0) {
        ocfg->programmable_initial_value[ss->num_programmable] = 0;
    } else {
#if 0 // TODO(ZX-3302): Disable until overflow interrupts are working.
        // The cycle counter is 64 bits so there's no need to check
        // |icfg->rate[ii]| here.
        if (icfg->rate[ii] > ss->max_programmable_value) {
            zxlogf(ERROR, "%s: Rate too large, event [%u]\n", __func__, ii);
            return ZX_ERR_INVALID_ARGS;
        }
        ocfg->programmable_initial_value[ss->num_programmable] =
            ss->max_programmable_value - icfg->rate[ii] + 1;
#else
        zxlogf(ERROR, "%s: data collection rates not supported yet\n", __func__);
        return ZX_ERR_NOT_SUPPORTED;
#endif
    }
    const EventDetails* details = NULL;
    switch (group) {
    case PERFMON_GROUP_ARCH:
        if (event >= kArchEventMapSize) {
            zxlogf(ERROR, "%s: Invalid event id, event [%u]\n", __func__, ii);
            return ZX_ERR_INVALID_ARGS;
        }
        details = &kArchEvents[kArchEventMap[event]];
        break;
    default:
        zxlogf(ERROR, "%s: Invalid event id, event [%u]\n", __func__, ii);
        return ZX_ERR_INVALID_ARGS;
    }
    // Arch events have at least ARM64_PMU_REG_FLAG_{ARCH,MICROARCH} set.
    if (details->flags == 0) {
        zxlogf(ERROR, "%s: Invalid event id, event [%u]\n", __func__, ii);
        return ZX_ERR_INVALID_ARGS;
    }
    ZX_DEBUG_ASSERT((details->flags & (ARM64_PMU_REG_FLAG_ARCH |
                                       ARM64_PMU_REG_FLAG_MICROARCH)) != 0);
    ocfg->programmable_hw_events[ss->num_programmable] = details->event;
    ocfg->programmable_flags[ss->num_programmable] = icfg->flags[ii];

    ++ss->num_programmable;
    return ZX_OK;
}

zx_status_t PerfmonDevice::PmuStageConfig(const void* cmd, size_t cmdlen) {
    zxlogf(TRACE, "%s called\n", __func__);

    if (active_) {
        return ZX_ERR_BAD_STATE;
    }
    PmuPerTraceState* per_trace = per_trace_state_.get();
    if (!per_trace) {
        return ZX_ERR_BAD_STATE;
    }

    // If we subsequently get an error, make sure any previous configuration
    // can't be used.
    per_trace->configured = false;

    perfmon_config_t* icfg = &per_trace->ioctl_config;
    if (cmdlen != sizeof(*icfg)) {
        return ZX_ERR_INVALID_ARGS;
    }
    memcpy(icfg, cmd, sizeof(*icfg));

    Arm64PmuConfig* ocfg = &per_trace->config;
    memset(ocfg, 0, sizeof(*ocfg));

    // Validate the config and convert it to our internal form.
    // TODO(dje): Multiplexing support.

    StagingState staging_state;
    StagingState* ss = &staging_state;
    ss->max_num_fixed = pmu_hw_properties_.num_fixed_events;
    ss->max_num_programmable = pmu_hw_properties_.num_programmable_events;
    ss->num_fixed = 0;
    ss->num_programmable = 0;
    ss->max_fixed_value =
        (pmu_hw_properties_.fixed_counter_width < 64
         ? (1ul << pmu_hw_properties_.fixed_counter_width) - 1
         : ~0ul);
    ss->max_programmable_value =
        (pmu_hw_properties_.programmable_counter_width < 64
         ? (1ul << pmu_hw_properties_.programmable_counter_width) - 1
         : ~0ul);
    ss->have_timebase0_user = false;

    zx_status_t status;
    unsigned ii;  // ii: input index
    for (ii = 0; ii < countof(icfg->events); ++ii) {
        perfmon_event_id_t id = icfg->events[ii];
        zxlogf(TRACE, "%s: processing [%u] = %u\n", __func__, ii, id);
        if (id == 0) {
            break;
        }
        unsigned group = PERFMON_EVENT_ID_GROUP(id);

        if (icfg->flags[ii] & ~PERFMON_CONFIG_FLAG_MASK) {
            zxlogf(ERROR, "%s: reserved flag bits set [%u]\n", __func__, ii);
            return ZX_ERR_INVALID_ARGS;
        }

        switch (group) {
        case PERFMON_GROUP_FIXED:
            status = pmu_stage_fixed_config(icfg, ss, ii, ocfg);
            if (status != ZX_OK) {
                return status;
            }
            break;
        case PERFMON_GROUP_ARCH:
            status = pmu_stage_programmable_config(icfg, ss, ii, ocfg);
            if (status != ZX_OK) {
                return status;
            }
            break;
        default:
            zxlogf(ERROR, "%s: Invalid event [%u] (bad group)\n",
                   __func__, ii);
            return ZX_ERR_INVALID_ARGS;
        }

        if (icfg->flags[ii] & PERFMON_CONFIG_FLAG_TIMEBASE0) {
            ss->have_timebase0_user = true;
        }
    }
    if (ii == 0) {
        zxlogf(ERROR, "%s: No events provided\n", __func__);
        return ZX_ERR_INVALID_ARGS;
    }

    // Ensure there are no holes.
    for (; ii < countof(icfg->events); ++ii) {
        if (icfg->events[ii] != PERFMON_EVENT_ID_NONE) {
            zxlogf(ERROR, "%s: Hole at event [%u]\n", __func__, ii);
            return ZX_ERR_INVALID_ARGS;
        }
        if (icfg->rate[ii] != 0) {
            zxlogf(ERROR, "%s: Hole at rate [%u]\n", __func__, ii);
            return ZX_ERR_INVALID_ARGS;
        }
        if (icfg->flags[ii] != 0) {
            zxlogf(ERROR, "%s: Hole at flags [%u]\n", __func__, ii);
            return ZX_ERR_INVALID_ARGS;
        }
    }

    if (ss->have_timebase0_user) {
        ocfg->timebase_event = icfg->events[0];
    }

    // TODO(dje): Basic sanity check that some data will be collected.

    per_trace->configured = true;
    return ZX_OK;
}

} // namespace perfmon
