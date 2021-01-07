// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// See the README.md in this directory for documentation.

#include <assert.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>

#include <ddk/debug.h>

#include "perf-mon.h"

namespace perfmon {

// There's only a few fixed events, so handle them directly.
enum FixedEventId {
#define DEF_FIXED_EVENT(symbol, event_name, id, regnum, flags, readable_name, description) \
  symbol##_ID = MakeEventId(kGroupFixed, id),
#include <lib/zircon-internal/device/cpu-trace/arm64-pm-events.inc>
};

// Verify each fixed counter regnum < ARM64_PMU_MAX_FIXED_COUNTERS.
#define DEF_FIXED_EVENT(symbol, event_name, id, regnum, flags, readable_name, description) \
  &&(regnum) < ARM64_PMU_MAX_FIXED_COUNTERS
static_assert(1
#include <lib/zircon-internal/device/cpu-trace/arm64-pm-events.inc>
              ,
              "");

enum ArchEvent {
#define DEF_ARCH_EVENT(symbol, event_name, id, pmceid_bit, event, flags, readable_name, \
                       description)                                                     \
  symbol,
#include <lib/zircon-internal/device/cpu-trace/arm64-pm-events.inc>
};

static const EventDetails kArchEvents[] = {
#define DEF_ARCH_EVENT(symbol, event_name, id, pmceid_bit, event, flags, readable_name, \
                       description)                                                     \
  {id, event, flags},
#include <lib/zircon-internal/device/cpu-trace/arm64-pm-events.inc>
};

// A table to map event id to index in |kArchEvents|.
// We use the kConstant naming style as once computed it is constant.
static const uint16_t* kArchEventMap;
static size_t kArchEventMapSize;

// Initialize the event maps.
// If there's a problem with the database just flag the error but don't crash.

static zx_status_t InitializeEventMaps() {
  zx_status_t status =
      BuildEventMap(kArchEvents, countof(kArchEvents), &kArchEventMap, &kArchEventMapSize);
  if (status != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

// Each arch provides its own |InitOnce()| method.

zx_status_t PerfmonDevice::InitOnce() {
  zx_status_t status = InitializeEventMaps();
  if (status != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

// Architecture-provided helpers for |PmuStageConfig()|.

void PerfmonDevice::InitializeStagingState(StagingState* ss) {
  ss->max_num_fixed = pmu_hw_properties_.common.max_num_fixed_events;
  ss->max_num_programmable = pmu_hw_properties_.common.max_num_programmable_events;
  ss->max_fixed_value = (pmu_hw_properties_.common.max_fixed_counter_width < 64
                             ? (1ul << pmu_hw_properties_.common.max_fixed_counter_width) - 1
                             : ~0ul);
  ss->max_programmable_value =
      (pmu_hw_properties_.common.max_programmable_counter_width < 64
           ? (1ul << pmu_hw_properties_.common.max_programmable_counter_width) - 1
           : ~0ul);
}

zx_status_t PerfmonDevice::StageFixedConfig(const FidlPerfmonConfig* icfg, StagingState* ss,
                                            unsigned input_index, PmuConfig* ocfg) {
  const unsigned ii = input_index;
  const EventId id = icfg->events[ii].event;
  EventRate rate = icfg->events[ii].rate;
  fidl_perfmon::EventConfigFlags flags = icfg->events[ii].flags;
  bool uses_timebase = ocfg->timebase_event != kEventIdNone && rate == 0;

  // There's only one fixed counter on ARM64, the cycle counter.
  if (id != FIXED_CYCLE_COUNTER_ID) {
    zxlogf(ERROR, "%s: Invalid fixed event [%u]", __func__, ii);
    return ZX_ERR_INVALID_ARGS;
  }
  if (ss->num_fixed > 0) {
    zxlogf(ERROR, "%s: Fixed event [%u] already provided", __func__, id);
    return ZX_ERR_INVALID_ARGS;
  }
  ocfg->fixed_events[ss->num_fixed] = id;

  if (rate == 0) {
    ocfg->fixed_initial_value[ss->num_fixed] = 0;
  } else {
#if 0  // TODO(fxbug.dev/33106): Disable until overflow interrupts are working.
       // The cycle counter is 64 bits so there's no need to check
       // |icfg->rate[ii]| here.
        ZX_DEBUG_ASSERT(ss->max_fixed_value == UINT64_MAX);
        ocfg->fixed_initial_value[ss->num_fixed] =
            ss->max_fixed_value - rate + 1;
#else
    zxlogf(ERROR, "%s: data collection rates not supported yet", __func__);
    return ZX_ERR_NOT_SUPPORTED;
#endif
  }

  // TODO(fxbug.dev/33106): Disable until overflow interrupts are working.
  if (uses_timebase) {
    zxlogf(ERROR, "%s: data collection rates not supported yet", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  uint32_t pmu_flags = 0;
  if (flags & fidl_perfmon::EventConfigFlags::COLLECT_OS) {
    pmu_flags |= kPmuConfigFlagOs;
  }
  if (flags & fidl_perfmon::EventConfigFlags::COLLECT_USER) {
    pmu_flags |= kPmuConfigFlagUser;
  }
  // TODO(fxbug.dev/33106): PC flag.
  ocfg->fixed_flags[ss->num_fixed] = pmu_flags;

  ++ss->num_fixed;
  return ZX_OK;
}

zx_status_t PerfmonDevice::StageProgrammableConfig(const FidlPerfmonConfig* icfg, StagingState* ss,
                                                   unsigned input_index, PmuConfig* ocfg) {
  const unsigned ii = input_index;
  EventId id = icfg->events[ii].event;
  unsigned group = GetEventIdGroup(id);
  unsigned event = GetEventIdEvent(id);
  EventRate rate = icfg->events[ii].rate;
  fidl_perfmon::EventConfigFlags flags = icfg->events[ii].flags;
  bool uses_timebase = ocfg->timebase_event != kEventIdNone && rate == 0;

  // TODO(dje): Verify no duplicates.
  if (ss->num_programmable == ss->max_num_programmable) {
    zxlogf(ERROR, "%s: Too many programmable counters provided", __func__);
    return ZX_ERR_INVALID_ARGS;
  }
  ocfg->programmable_events[ss->num_programmable] = id;

  if (rate == 0) {
    ocfg->programmable_initial_value[ss->num_programmable] = 0;
  } else {
#if 0  // TODO(fxbug.dev/33106): Disable until overflow interrupts are working.
       // The cycle counter is 64 bits so there's no need to check
       // |icfg->rate[ii]| here.
        if (icfg->rate[ii] > ss->max_programmable_value) {
            zxlogf(ERROR, "%s: Rate too large, event [%u]", __func__, ii);
            return ZX_ERR_INVALID_ARGS;
        }
        ocfg->programmable_initial_value[ss->num_programmable] =
            ss->max_programmable_value - icfg->rate[ii] + 1;
#else
    zxlogf(ERROR, "%s: data collection rates not supported yet", __func__);
    return ZX_ERR_NOT_SUPPORTED;
#endif
  }

  const EventDetails* details = NULL;
  switch (group) {
    case kGroupArch:
      if (event >= kArchEventMapSize) {
        zxlogf(ERROR, "%s: Invalid event id, event [%u]", __func__, ii);
        return ZX_ERR_INVALID_ARGS;
      }
      details = &kArchEvents[kArchEventMap[event]];
      break;
    default:
      zxlogf(ERROR, "%s: Invalid event id, event [%u]", __func__, ii);
      return ZX_ERR_INVALID_ARGS;
  }
  // Arch events have at least ARM64_PMU_REG_FLAG_{ARCH,MICROARCH} set.
  if (details->flags == 0) {
    zxlogf(ERROR, "%s: Invalid event id, event [%u]", __func__, ii);
    return ZX_ERR_INVALID_ARGS;
  }
  ZX_DEBUG_ASSERT((details->flags & (ARM64_PMU_REG_FLAG_ARCH | ARM64_PMU_REG_FLAG_MICROARCH)) != 0);

  ocfg->programmable_hw_events[ss->num_programmable] = details->event;

  // TODO(fxbug.dev/33106): Disable until overflow interrupts are working.
  if (uses_timebase) {
    zxlogf(ERROR, "%s: data collection rates not supported yet", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  uint32_t pmu_flags = 0;
  if (flags & fidl_perfmon::EventConfigFlags::COLLECT_OS) {
    pmu_flags |= kPmuConfigFlagOs;
  }
  if (flags & fidl_perfmon::EventConfigFlags::COLLECT_USER) {
    pmu_flags |= kPmuConfigFlagUser;
  }
  // TODO(fxbug.dev/33106): PC flag.
  ocfg->programmable_flags[ss->num_programmable] = pmu_flags;

  ++ss->num_programmable;
  return ZX_OK;
}

zx_status_t PerfmonDevice::StageMiscConfig(const FidlPerfmonConfig* icfg, StagingState* ss,
                                           unsigned input_index, PmuConfig* ocfg) {
  // There are no misc events yet.
  zxlogf(ERROR, "%s: Invalid event [%u] (no misc events)", __func__, input_index);
  return ZX_ERR_INVALID_ARGS;
}

}  // namespace perfmon
