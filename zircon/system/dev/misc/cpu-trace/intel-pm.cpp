// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// See the README.md in this directory for documentation.

#include <assert.h>
#include <cpuid.h>

#include <ddk/debug.h>
#include <ddk/protocol/platform/device.h>

#include <fbl/alloc_checker.h>

#include "perf-mon.h"

namespace perfmon {

// TODO(dje): Having trouble getting this working, so just punt for now.
#define TRY_FREEZE_ON_PMI 0

// Individual bits in the fixed counter enable field.
// See Intel Volume 3, Figure 18-2 "Layout of IA32_FIXED_CTR_CTRL MSR".
#define FIXED_CTR_ENABLE_OS 1
#define FIXED_CTR_ENABLE_USR 2

// This table is sorted at startup.
static perfmon_event_id_t misc_event_table_contents[IPM_NUM_MISC_EVENTS] = {
#define DEF_MISC_SKL_EVENT(symbol, event_name, id, offset, size, flags, readable_name, description) \
    PERFMON_MAKE_EVENT_ID(PERFMON_GROUP_MISC, id),
#include <lib/zircon-internal/device/cpu-trace/skylake-misc-events.inc>
};

// Const accessor to give the illusion of the table being const.
static const perfmon_event_id_t* misc_event_table = &misc_event_table_contents[0];

enum ArchEvent : uint16_t {
#define DEF_ARCH_EVENT(symbol, event_name, id, ebx_bit, event, umask, flags, readable_name, description) \
    symbol,
#include <lib/zircon-internal/device/cpu-trace/intel-pm-events.inc>
};

enum ModelEvent : uint16_t {
#define DEF_SKL_EVENT(symbol, event_name, id, event, umask, flags, readable_name, description) \
    symbol,
#include <lib/zircon-internal/device/cpu-trace/skylake-pm-events.inc>
};

static const EventDetails kArchEvents[] = {
#define DEF_ARCH_EVENT(symbol, event_name, id, ebx_bit, event, umask, flags, readable_name, description) \
    { id, event, umask, flags },
#include <lib/zircon-internal/device/cpu-trace/intel-pm-events.inc>
};

static const EventDetails kModelEvents[] = {
#define DEF_SKL_EVENT(symbol, event_name, id, event, umask, flags, readable_name, description) \
    { id, event, umask, flags },
#include <lib/zircon-internal/device/cpu-trace/skylake-pm-events.inc>
};

// A table to map event id to index in |kArchEvents|.
// We use the kConstant naming style as once computed it is constant.
static const uint16_t* kArchEventMap;
static size_t kArchEventMapSize;

// A table to map event id to index in |kModelEvents|.
// We use the kConstant naming style as once computed it is constant.
static const uint16_t* kModelEventMap;
static size_t kModelEventMapSize;

// Map a fixed counter event id to its h/w register number.
// Returns IPM_MAX_FIXED_COUNTERS if |id| is unknown.
static unsigned PmuFixedCounterNumber(perfmon_event_id_t id) {
    enum {
#define DEF_FIXED_EVENT(symbol, event_name, id, regnum, flags, readable_name, description) \
        symbol ## _NUMBER = regnum,
#include <lib/zircon-internal/device/cpu-trace/intel-pm-events.inc>
    };
    switch (id) {
    case FIXED_INSTRUCTIONS_RETIRED_ID:
        return FIXED_INSTRUCTIONS_RETIRED_NUMBER;
    case FIXED_UNHALTED_CORE_CYCLES_ID:
        return FIXED_UNHALTED_CORE_CYCLES_NUMBER;
    case FIXED_UNHALTED_REFERENCE_CYCLES_ID:
        return FIXED_UNHALTED_REFERENCE_CYCLES_NUMBER;
    default:
        return IPM_MAX_FIXED_COUNTERS;
    }
}

static void PmuInitMiscEventTable() {
    qsort(misc_event_table_contents,
          countof(misc_event_table_contents),
          sizeof(misc_event_table_contents[0]),
          ComparePerfmonEventId);
}

// Map a misc event id to its ordinal (unique number in range
// 0 ... IPM_NUM_MISC_EVENTS - 1).
// Returns -1 if |id| is unknown.
static int PmuLookupMiscEvent(perfmon_event_id_t id) {
    auto p = reinterpret_cast<perfmon_event_id_t*>(
        bsearch(&id, misc_event_table,
                countof(misc_event_table_contents),
                sizeof(id),
                ComparePerfmonEventId));
    if (!p) {
        return -1;
    }
    ptrdiff_t result = p - misc_event_table;
    assert(result < IPM_NUM_MISC_EVENTS);
    return (int) result;
}

// Initialize the event maps.
// If there's a problem with the database just flag the error but don't crash.

static zx_status_t InitializeEventMaps() {
    PmuInitMiscEventTable();

    zx_status_t status = BuildEventMap(kArchEvents, countof(kArchEvents),
                                       &kArchEventMap, &kArchEventMapSize);
    if (status != ZX_OK) {
        return status;
    }

    status = BuildEventMap(kModelEvents, countof(kModelEvents),
                           &kModelEventMap, &kModelEventMapSize);
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

    // Skylake supports version 4. KISS and begin with that.
    // Note: This should agree with the kernel driver's check.
    if (pmu_hw_properties_.pm_version < 4) {
        zxlogf(INFO, "%s: PM version 4 or above is required\n", __func__);
        return ZX_ERR_NOT_SUPPORTED;
    }

    status = InitializeEventMaps();
    if (status != ZX_OK) {
        return status;
    }

    zxlogf(TRACE, "Intel Performance Monitor configuration for this chipset:\n");
    zxlogf(TRACE, "IPM: version: %u\n", pmu_hw_properties_.pm_version);
    zxlogf(TRACE, "IPM: num_programmable_events: %u\n",
           pmu_hw_properties_.num_programmable_events);
    zxlogf(TRACE, "IPM: num_fixed_events: %u\n",
           pmu_hw_properties_.num_fixed_events);
    zxlogf(TRACE, "IPM: num_misc_events: %u\n",
           pmu_hw_properties_.num_misc_events);
    zxlogf(TRACE, "IPM: programmable_counter_width: %u\n",
           pmu_hw_properties_.programmable_counter_width);
    zxlogf(TRACE, "IPM: fixed_counter_width: %u\n",
           pmu_hw_properties_.fixed_counter_width);
    zxlogf(TRACE, "IPM: perf_capabilities: 0x%lx\n",
           pmu_hw_properties_.perf_capabilities);

    return ZX_OK;
}


// Architecture-provided helpers for |PmuStageConfig()|.

static bool LbrSupported() {
    return PerfmonDevice::pmu_hw_properties().lbr_stack_size > 0;
}

void PerfmonDevice::InitializeStagingState(StagingState* ss) {
    ss->max_num_fixed = pmu_hw_properties_.num_fixed_events;
    ss->max_num_programmable = pmu_hw_properties_.num_programmable_events;
    ss->max_num_misc = pmu_hw_properties_.num_misc_events;
    ss->max_fixed_value =
        (pmu_hw_properties_.fixed_counter_width < 64
         ? (1ul << pmu_hw_properties_.fixed_counter_width) - 1
         : ~0ul);
    ss->max_programmable_value =
        (pmu_hw_properties_.programmable_counter_width < 64
         ? (1ul << pmu_hw_properties_.programmable_counter_width) - 1
         : ~0ul);
}

zx_status_t PerfmonDevice::StageFixedConfig(const perfmon_config_t* icfg,
                                            StagingState* ss,
                                            unsigned input_index,
                                            PmuConfig* ocfg) {
    const unsigned ii = input_index;
    const perfmon_event_id_t id = icfg->events[ii];
    bool uses_timebase0 = !!(icfg->flags[ii] & PERFMON_CONFIG_FLAG_TIMEBASE0);
    unsigned counter = PmuFixedCounterNumber(id);

    if (counter == IPM_MAX_FIXED_COUNTERS ||
            counter >= countof(ocfg->fixed_events) ||
            counter >= ss->max_num_fixed) {
        zxlogf(ERROR, "%s: Invalid fixed event [%u]\n", __func__, ii);
        return ZX_ERR_INVALID_ARGS;
    }
    if (ss->have_fixed[counter]) {
        zxlogf(ERROR, "%s: Fixed event [%u] already provided\n",
               __func__, counter);
        return ZX_ERR_INVALID_ARGS;
    }
    ss->have_fixed[counter] = true;
    ocfg->fixed_events[ss->num_fixed] = id;
    if ((uses_timebase0 && input_index != 0) || icfg->rate[ii] == 0) {
        ocfg->fixed_initial_value[ss->num_fixed] = 0;
    } else {
        if (icfg->rate[ii] > ss->max_fixed_value) {
            zxlogf(ERROR, "%s: Rate too large, event [%u]\n", __func__, ii);
            return ZX_ERR_INVALID_ARGS;
        }
        ocfg->fixed_initial_value[ss->num_fixed] =
            ss->max_fixed_value - icfg->rate[ii] + 1;
    }
    // KISS: For now don't generate PMI's for counters that use
    // another as the timebase.
    if (!uses_timebase0 || ii == 0) {
        ocfg->fixed_ctrl |= IA32_FIXED_CTR_CTRL_PMI_MASK(counter);
    }
    unsigned enable = 0;
    if (icfg->flags[ii] & PERFMON_CONFIG_FLAG_OS) {
        enable |= FIXED_CTR_ENABLE_OS;
    }
    if (icfg->flags[ii] & PERFMON_CONFIG_FLAG_USER) {
        enable |= FIXED_CTR_ENABLE_USR;
    }
    ocfg->fixed_ctrl |= enable << IA32_FIXED_CTR_CTRL_EN_SHIFT(counter);
    ocfg->global_ctrl |= IA32_PERF_GLOBAL_CTRL_FIXED_EN_MASK(counter);
    if (icfg->flags[ii] & PERFMON_CONFIG_FLAG_TIMEBASE0) {
        ocfg->fixed_flags[ss->num_fixed] |= kPmuConfigFlagTimebase0;
    }
    if (icfg->flags[ii] & PERFMON_CONFIG_FLAG_PC) {
        ocfg->fixed_flags[ss->num_fixed] |= kPmuConfigFlagPc;
    }
    if (icfg->flags[ii] & PERFMON_CONFIG_FLAG_LAST_BRANCH) {
        if (!LbrSupported()) {
            zxlogf(ERROR, "%s: Last branch not supported, event [%u]\n"
                   , __func__, ii);
            return ZX_ERR_INVALID_ARGS;
        }
        if (icfg->rate[ii] == 0 ||
                ((icfg->flags[ii] & PERFMON_CONFIG_FLAG_TIMEBASE0) &&
                 ii != 0)) {
            zxlogf(ERROR, "%s: Last branch requires own timebase, event [%u]\n"
                   , __func__, ii);
            return ZX_ERR_INVALID_ARGS;
        }
        ocfg->fixed_flags[ss->num_fixed] |= kPmuConfigFlagLastBranch;
        ocfg->debug_ctrl |= IA32_DEBUGCTL_LBR_MASK;
    }

    ++ss->num_fixed;
    return ZX_OK;
}

zx_status_t PerfmonDevice::StageProgrammableConfig(const perfmon_config_t* icfg,
                                                   StagingState* ss,
                                                   unsigned input_index,
                                                   PmuConfig* ocfg) {
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
        if (icfg->rate[ii] > ss->max_programmable_value) {
            zxlogf(ERROR, "%s: Rate too large, event [%u]\n", __func__, ii);
            return ZX_ERR_INVALID_ARGS;
        }
        ocfg->programmable_initial_value[ss->num_programmable] =
            ss->max_programmable_value - icfg->rate[ii] + 1;
    }
    const EventDetails* details = nullptr;
    switch (group) {
    case PERFMON_GROUP_ARCH:
        if (event >= kArchEventMapSize) {
            zxlogf(ERROR, "%s: Invalid event id, event [%u]\n", __func__, ii);
            return ZX_ERR_INVALID_ARGS;
        }
        details = &kArchEvents[kArchEventMap[event]];
        break;
    case PERFMON_GROUP_MODEL:
        if (event >= kModelEventMapSize) {
            zxlogf(ERROR, "%s: Invalid event id, event [%u]\n", __func__, ii);
            return ZX_ERR_INVALID_ARGS;
        }
        details = &kModelEvents[kModelEventMap[event]];
        break;
    default:
        zxlogf(ERROR, "%s: Invalid event id, event [%u]\n", __func__, ii);
        return ZX_ERR_INVALID_ARGS;
    }
    if (details->event == 0 && details->umask == 0) {
        zxlogf(ERROR, "%s: Invalid event id, event [%u]\n", __func__, ii);
        return ZX_ERR_INVALID_ARGS;
    }
    uint64_t evtsel = 0;
    evtsel |= details->event << IA32_PERFEVTSEL_EVENT_SELECT_SHIFT;
    evtsel |= details->umask << IA32_PERFEVTSEL_UMASK_SHIFT;
    if (icfg->flags[ii] & PERFMON_CONFIG_FLAG_OS) {
        evtsel |= IA32_PERFEVTSEL_OS_MASK;
    }
    if (icfg->flags[ii] & PERFMON_CONFIG_FLAG_USER) {
        evtsel |= IA32_PERFEVTSEL_USR_MASK;
    }
    if (details->flags & IPM_REG_FLAG_EDG) {
        evtsel |= IA32_PERFEVTSEL_E_MASK;
    }
    if (details->flags & IPM_REG_FLAG_ANYT) {
        evtsel |= IA32_PERFEVTSEL_ANY_MASK;
    }
    if (details->flags & IPM_REG_FLAG_INV) {
        evtsel |= IA32_PERFEVTSEL_INV_MASK;
    }
    evtsel |= (details->flags & IPM_REG_FLAG_CMSK_MASK) << IA32_PERFEVTSEL_CMASK_SHIFT;
    // KISS: For now don't generate PMI's for counters that use
    // another as the timebase. We still generate interrupts in
    // "counting mode" in case the counter overflows.
    if (!uses_timebase0 || ii == 0) {
        evtsel |= IA32_PERFEVTSEL_INT_MASK;
    }
    evtsel |= IA32_PERFEVTSEL_EN_MASK;
    ocfg->programmable_hw_events[ss->num_programmable] = evtsel;
    ocfg->global_ctrl |= IA32_PERF_GLOBAL_CTRL_PMC_EN_MASK(ss->num_programmable);
    if (icfg->flags[ii] & PERFMON_CONFIG_FLAG_TIMEBASE0) {
        ocfg->programmable_flags[ss->num_programmable] |= kPmuConfigFlagTimebase0;
    }
    if (icfg->flags[ii] & PERFMON_CONFIG_FLAG_PC) {
        ocfg->programmable_flags[ss->num_programmable] |= kPmuConfigFlagPc;
    }
    if (icfg->flags[ii] & PERFMON_CONFIG_FLAG_LAST_BRANCH) {
        if (!LbrSupported()) {
            zxlogf(ERROR, "%s: Last branch not supported, event [%u]\n"
                   , __func__, ii);
            return ZX_ERR_INVALID_ARGS;
        }
        if (icfg->rate[ii] == 0 ||
                ((icfg->flags[ii] & PERFMON_CONFIG_FLAG_TIMEBASE0) &&
                 ii != 0)) {
            zxlogf(ERROR, "%s: Last branch requires own timebase, event [%u]\n"
                   , __func__, ii);
            return ZX_ERR_INVALID_ARGS;
        }
        ocfg->programmable_flags[ss->num_programmable] |= kPmuConfigFlagLastBranch;
        ocfg->debug_ctrl |= IA32_DEBUGCTL_LBR_MASK;
    }

    ++ss->num_programmable;
    return ZX_OK;
}

zx_status_t PerfmonDevice::StageMiscConfig(const perfmon_config_t* icfg,
                                              StagingState* ss,
                                              unsigned input_index,
                                              PmuConfig* ocfg) {
    const unsigned ii = input_index;
    perfmon_event_id_t id = icfg->events[ii];
    int event = PmuLookupMiscEvent(id);

    if (event < 0) {
        zxlogf(ERROR, "%s: Invalid misc event [%u]\n", __func__, ii);
        return ZX_ERR_INVALID_ARGS;
    }
    if (ss->num_misc == ss->max_num_misc) {
        zxlogf(ERROR, "%s: Too many misc counters provided\n",
               __func__);
        return ZX_ERR_INVALID_ARGS;
    }
    if (ss->have_misc[event / 64] & (1ul << (event % 64))) {
        zxlogf(ERROR, "%s: Misc event [%u] already provided\n",
               __func__, ii);
        return ZX_ERR_INVALID_ARGS;
    }
    ss->have_misc[event / 64] |= 1ul << (event % 64);
    ocfg->misc_events[ss->num_misc] = id;
    if (icfg->flags[ii] & PERFMON_CONFIG_FLAG_TIMEBASE0) {
        ocfg->misc_flags[ss->num_misc] |= kPmuConfigFlagTimebase0;
    } else {
        if (icfg->rate[ii] != 0) {
            zxlogf(ERROR, "%s: Misc event [%u] requires a timebase\n",
                   __func__, ii);
            return ZX_ERR_INVALID_ARGS;
        }
    }

    ++ss->num_misc;
    return ZX_OK;
}

zx_status_t PerfmonDevice::VerifyStaging(StagingState* ss, PmuConfig* ocfg) {
    PmuPerTraceState* per_trace = per_trace_state_.get();

    // Require something to be enabled in order to start tracing.
    // This is mostly a sanity check.
    if (per_trace->config.global_ctrl == 0) {
        zxlogf(ERROR, "%s: Requested config doesn't collect any data\n",
               __func__);
        return ZX_ERR_INVALID_ARGS;
    }

#if TRY_FREEZE_ON_PMI
    ocfg->debug_ctrl |= IA32_DEBUGCTL_FREEZE_PERFMON_ON_PMI_MASK;
#endif

    return ZX_OK;
}

} // namespace perfmon
