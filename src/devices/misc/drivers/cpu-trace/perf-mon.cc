// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// See the README.md in this directory for documentation.

#include "perf-mon.h"

#include <assert.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/zircon-internal/device/cpu-trace/perf-mon.h>
#include <lib/zircon-internal/mtrace.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <cstdint>
#include <limits>
#include <memory>

#include <ddktl/fidl.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

#include "cpu-trace-private.h"

namespace perfmon {

int ComparePerfmonEventId(const void* ap, const void* bp) {
  auto a = reinterpret_cast<const EventId*>(ap);
  auto b = reinterpret_cast<const EventId*>(bp);
  if (*a < *b) {
    return -1;
  }
  if (*a > *b) {
    return 1;
  }
  return 0;
}

uint16_t GetLargestEventId(const EventDetails* events, size_t count) {
  uint16_t largest = 0;

  for (size_t i = 0; i < count; ++i) {
    uint16_t id = events[i].id;
    if (id > largest) {
      largest = id;
    }
  }

  return largest;
}

zx_status_t BuildEventMap(const EventDetails* events, size_t count, const uint16_t** out_event_map,
                          size_t* out_map_size) {
  static_assert(kMaxEvent < std::numeric_limits<uint16_t>::max());

  uint16_t largest_event_id = GetLargestEventId(events, count);
  // See perf-mon.h: The full event id is split into two pieces:
  // group type and id within that group. The event recorded in
  // |EventDetails| is the id within the group. Each id must be in
  // the range [1,PERFMON_MAX_EVENT]. ID 0 is reserved.
  if (largest_event_id == 0 || largest_event_id > kMaxEvent) {
    zxlogf(ERROR, "PMU: Corrupt event database");
    return ZX_ERR_INTERNAL;
  }

  fbl::AllocChecker ac;
  size_t event_map_size = largest_event_id + 1;
  zxlogf(DEBUG, "PMU: %zu arch events", count);
  zxlogf(DEBUG, "PMU: arch event id range: 1-%zu", event_map_size);
  auto event_map = std::unique_ptr<uint16_t[]>(new (&ac) uint16_t[event_map_size]{});
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  for (uint16_t i = 0; i < count; ++i) {
    uint16_t id = events[i].id;
    assert(id < event_map_size);
    assert(event_map[id] == 0);
    event_map[id] = i;
  }

  *out_event_map = event_map.release();
  *out_map_size = event_map_size;
  return ZX_OK;
}

static void DumpHwProperties(const PmuHwProperties& props) {
  zxlogf(INFO, "Performance Monitor Unit configuration for this chipset:");
  zxlogf(INFO, "PMU: version %u", props.common.pm_version);
  zxlogf(INFO, "PMU: %u fixed events, width %u", props.common.max_num_fixed_events,
         props.common.max_fixed_counter_width);
  zxlogf(INFO, "PMU: %u programmable events, width %u", props.common.max_num_programmable_events,
         props.common.max_programmable_counter_width);
  zxlogf(INFO, "PMU: %u misc events, width %u", props.common.max_num_misc_events,
         props.common.max_misc_counter_width);
#ifdef __x86_64__
  zxlogf(INFO, "PMU: perf_capabilities: 0x%lx", props.perf_capabilities);
  zxlogf(INFO, "PMU: lbr_stack_size: %u", props.lbr_stack_size);
#endif
}

PerfmonDevice::PerfmonDevice(zx_device_t* parent, zx::bti bti, perfmon::PmuHwProperties props,
                             mtrace_control_func_t* mtrace_control)
    : DeviceType(parent),
      bti_(std::move(bti)),
      pmu_hw_properties_(props),
      mtrace_control_(mtrace_control) {}

zx_status_t PerfmonDevice::GetHwProperties(mtrace_control_func_t* mtrace_control,
                                           PmuHwProperties* out_props) {
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  zx_handle_t resource = get_root_resource();
  zx_status_t status = mtrace_control(resource, MTRACE_KIND_PERFMON, MTRACE_PERFMON_GET_PROPERTIES,
                                      0, out_props, sizeof(*out_props));
  if (status != ZX_OK) {
    if (status == ZX_ERR_NOT_SUPPORTED) {
      zxlogf(INFO, "%s: No PM support", __func__);
    } else {
      zxlogf(INFO, "%s: Error %d fetching ipm properties", __func__, status);
    }
    return status;
  }

  return ZX_OK;
}

void PerfmonDevice::FreeBuffersForTrace(PmuPerTraceState* per_trace, uint32_t num_allocated) {
  // Note: This may be called with partially allocated buffers.
  assert(per_trace->buffers);
  assert(num_allocated <= per_trace->num_buffers);
  for (uint32_t i = 0; i < num_allocated; ++i) {
    io_buffer_release(&per_trace->buffers[i]);
  }
  per_trace->buffers.reset();
}

void PerfmonDevice::PmuGetProperties(FidlPerfmonProperties* props) {
  zxlogf(DEBUG, "%s called", __func__);

  props->api_version = fidl_perfmon::wire::kApiVersion;
  props->pm_version = pmu_hw_properties_.common.pm_version;
  static_assert(perfmon::kMaxNumEvents == fidl_perfmon::wire::kMaxNumEvents);
  props->max_num_events = fidl_perfmon::wire::kMaxNumEvents;

  // These numbers are for informational/debug purposes. There can be
  // further restrictions and limitations.
  // TODO(dje): Something more elaborate can wait for publishing them via
  // some namespace.
  props->max_num_fixed_events = pmu_hw_properties_.common.max_num_fixed_events;
  props->max_fixed_counter_width = pmu_hw_properties_.common.max_fixed_counter_width;
  props->max_num_programmable_events = pmu_hw_properties_.common.max_num_programmable_events;
  props->max_programmable_counter_width = pmu_hw_properties_.common.max_programmable_counter_width;
  props->max_num_misc_events = pmu_hw_properties_.common.max_num_misc_events;
  props->max_misc_counter_width = pmu_hw_properties_.common.max_misc_counter_width;

  props->flags = fidl_perfmon::wire::PropertyFlags();
#ifdef __x86_64__
  if (pmu_hw_properties_.lbr_stack_size > 0) {
    props->flags |= fidl_perfmon::wire::PropertyFlags::kHasLastBranch;
  }
#endif
}

zx_status_t PerfmonDevice::PmuInitialize(const FidlPerfmonAllocation* allocation) {
  zxlogf(DEBUG, "%s called", __func__);

  if (per_trace_state_) {
    return ZX_ERR_BAD_STATE;
  }

  uint32_t num_cpus = zx_system_get_num_cpus();
  if (allocation->num_buffers != num_cpus) {  // TODO(dje): for now
    return ZX_ERR_INVALID_ARGS;
  }
  if (allocation->buffer_size_in_pages > kMaxPerTraceSpaceInPages) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AllocChecker ac;
  auto per_trace = std::unique_ptr<PmuPerTraceState>(new (&ac) PmuPerTraceState{});
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  per_trace->buffers = std::unique_ptr<io_buffer_t[]>(new (&ac) io_buffer_t[num_cpus]);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  size_t buffer_size = allocation->buffer_size_in_pages * kPageSize;
  uint32_t i = 0;
  for (; i < num_cpus; ++i) {
    zx_status_t status =
        io_buffer_init(&per_trace->buffers[i], bti_.get(), buffer_size, IO_BUFFER_RW);
    if (status != ZX_OK) {
      break;
    }
  }
  if (i != num_cpus) {
    FreeBuffersForTrace(per_trace.get(), i);
    return ZX_ERR_NO_MEMORY;
  }

  per_trace->num_buffers = allocation->num_buffers;
  per_trace->buffer_size_in_pages = allocation->buffer_size_in_pages;
  per_trace_state_ = std::move(per_trace);
  return ZX_OK;
}

void PerfmonDevice::PmuTerminate() {
  zxlogf(DEBUG, "%s called", __func__);

  if (active_) {
    PmuStop();
  }

  PmuPerTraceState* per_trace = per_trace_state_.get();
  if (per_trace) {
    FreeBuffersForTrace(per_trace, per_trace->num_buffers);
    per_trace_state_.reset();
  }
}

zx_status_t PerfmonDevice::PmuGetAllocation(FidlPerfmonAllocation* allocation) {
  zxlogf(DEBUG, "%s called", __func__);

  const PmuPerTraceState* per_trace = per_trace_state_.get();
  if (!per_trace) {
    return ZX_ERR_BAD_STATE;
  }

  allocation->num_buffers = per_trace->num_buffers;
  allocation->buffer_size_in_pages = per_trace->buffer_size_in_pages;
  return ZX_OK;
}

zx_status_t PerfmonDevice::PmuGetBufferHandle(uint32_t descriptor, zx_handle_t* out_handle) {
  zxlogf(DEBUG, "%s called", __func__);

  const PmuPerTraceState* per_trace = per_trace_state_.get();
  if (!per_trace) {
    return ZX_ERR_BAD_STATE;
  }

  if (descriptor >= per_trace->num_buffers) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_handle_t h;
  zx_status_t status =
      zx_handle_duplicate(per_trace->buffers[descriptor].vmo_handle, ZX_RIGHT_SAME_RIGHTS, &h);
  if (status != ZX_OK) {
    // This failure could be hard to debug. Give the user some help.
    zxlogf(ERROR, "%s: Failed to duplicate %u buffer handle: %d", __func__, descriptor, status);
    return status;
  }

  *out_handle = h;
  return ZX_OK;
}

// Do an architecture-independent verification pass over |icfg|,
// and see if there's a timebase event.
static zx_status_t VerifyAndCheckTimebase(const FidlPerfmonConfig* icfg, PmuConfig* ocfg) {
  unsigned ii;  // ii: input index
  for (ii = 0; ii < countof(icfg->events); ++ii) {
    EventId id = icfg->events[ii].event;
    if (id == kEventIdNone) {
      break;
    }
    EventRate rate = icfg->events[ii].rate;
    fidl_perfmon::wire::EventConfigFlags flags = icfg->events[ii].flags;

    if (flags & fidl_perfmon::wire::EventConfigFlags::kIsTimebase) {
      if (ocfg->timebase_event != kEventIdNone) {
        zxlogf(ERROR, "%s: multiple timebases [%u]", __func__, ii);
        return ZX_ERR_INVALID_ARGS;
      }
      ocfg->timebase_event = icfg->events[ii].event;
    }

    if (flags & fidl_perfmon::wire::EventConfigFlags::kCollectPc) {
      if (rate == 0) {
        zxlogf(ERROR, "%s: PC flag requires own timebase, event [%u]", __func__, ii);
        return ZX_ERR_INVALID_ARGS;
      }
    }

    if (flags & fidl_perfmon::wire::EventConfigFlags::kCollectLastBranch) {
      // Further verification is architecture specific.
      if (icfg->events[ii].rate == 0) {
        zxlogf(ERROR, "%s: Last branch requires own timebase, event [%u]", __func__, ii);
        return ZX_ERR_INVALID_ARGS;
      }
    }
  }

  if (ii == 0) {
    zxlogf(ERROR, "%s: No events provided", __func__);
    return ZX_ERR_INVALID_ARGS;
  }

  // Ensure there are no holes.
  for (; ii < countof(icfg->events); ++ii) {
    if (icfg->events[ii].event != kEventIdNone) {
      zxlogf(ERROR, "%s: Hole at event [%u]", __func__, ii);
      return ZX_ERR_INVALID_ARGS;
    }
    if (icfg->events[ii].rate != 0) {
      zxlogf(ERROR, "%s: Hole at rate [%u]", __func__, ii);
      return ZX_ERR_INVALID_ARGS;
    }
    if (icfg->events[ii].flags != fidl_perfmon::wire::EventConfigFlags()) {
      zxlogf(ERROR, "%s: Hole at flags [%u]", __func__, ii);
      return ZX_ERR_INVALID_ARGS;
    }
  }

  return ZX_OK;
}

zx_status_t PerfmonDevice::PmuStageConfig(const FidlPerfmonConfig* fidl_config) {
  zxlogf(DEBUG, "%s called", __func__);

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

  const FidlPerfmonConfig* icfg = fidl_config;

  PmuConfig* ocfg = &per_trace->config;
  *ocfg = {};

  // Validate the config and convert it to our internal form.
  // TODO(dje): Multiplexing support.

  StagingState staging_state{};
  StagingState* ss = &staging_state;
  InitializeStagingState(ss);

  zx_status_t status = VerifyAndCheckTimebase(icfg, ocfg);
  if (status != ZX_OK) {
    return status;
  }

  unsigned ii;  // ii: input index
  for (ii = 0; ii < icfg->events.size(); ++ii) {
    EventId id = icfg->events[ii].event;
    zxlogf(DEBUG, "%s: processing [%u] = %u", __func__, ii, id);
    if (id == kEventIdNone) {
      break;
    }
    unsigned group = GetEventIdGroup(id);

    switch (group) {
      case kGroupFixed:
        status = StageFixedConfig(icfg, ss, ii, ocfg);
        if (status != ZX_OK) {
          return status;
        }
        break;
      case kGroupArch:
      case kGroupModel:
        status = StageProgrammableConfig(icfg, ss, ii, ocfg);
        if (status != ZX_OK) {
          return status;
        }
        break;
      case kGroupMisc:
        status = StageMiscConfig(icfg, ss, ii, ocfg);
        if (status != ZX_OK) {
          return status;
        }
        break;
      default:
        zxlogf(ERROR, "%s: Invalid event [%u] (bad group)", __func__, ii);
        return ZX_ERR_INVALID_ARGS;
    }
  }

  // TODO(dje): Basic sanity check that some data will be collected.

  per_trace->fidl_config = *icfg;
  per_trace->configured = true;
  return ZX_OK;
}

zx_status_t PerfmonDevice::PmuGetConfig(FidlPerfmonConfig* config) {
  zxlogf(DEBUG, "%s called", __func__);

  const PmuPerTraceState* per_trace = per_trace_state_.get();
  if (!per_trace) {
    return ZX_ERR_BAD_STATE;
  }

  if (!per_trace->configured) {
    return ZX_ERR_BAD_STATE;
  }

  *config = per_trace->fidl_config;
  return ZX_OK;
}

zx_status_t PerfmonDevice::PmuStart() {
  zxlogf(DEBUG, "%s called", __func__);

  if (active_) {
    return ZX_ERR_BAD_STATE;
  }
  PmuPerTraceState* per_trace = per_trace_state_.get();
  if (!per_trace) {
    return ZX_ERR_BAD_STATE;
  }

  if (!per_trace->configured) {
    return ZX_ERR_BAD_STATE;
  }

  // Step 1: Get the configuration data into the kernel for use by START.

#ifdef __x86_64__
  zxlogf(DEBUG, "%s: global ctrl 0x%lx, fixed ctrl 0x%lx", __func__, per_trace->config.global_ctrl,
         per_trace->config.fixed_ctrl);

  // Note: If only misc counters are enabled then |global_ctrl| will be zero.
#endif

  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  zx_handle_t resource = get_root_resource();

  zx_status_t status =
      mtrace_control_(resource, MTRACE_KIND_PERFMON, MTRACE_PERFMON_INIT, 0, nullptr, 0);
  if (status != ZX_OK) {
    return status;
  }

  uint32_t num_cpus = zx_system_get_num_cpus();
  for (uint32_t cpu = 0; cpu < num_cpus; ++cpu) {
    zx_pmu_buffer_t buffer;
    io_buffer_t* io_buffer = &per_trace->buffers[cpu];
    buffer.vmo = io_buffer->vmo_handle;
    status = mtrace_control_(resource, MTRACE_KIND_PERFMON, MTRACE_PERFMON_ASSIGN_BUFFER, cpu,
                             &buffer, sizeof(buffer));
    if (status != ZX_OK) {
      goto fail;
    }
  }

  status = mtrace_control_(resource, MTRACE_KIND_PERFMON, MTRACE_PERFMON_STAGE_CONFIG, 0,
                           &per_trace->config, sizeof(per_trace->config));
  if (status != ZX_OK) {
    goto fail;
  }

  // Step 2: Start data collection.

  status = mtrace_control_(resource, MTRACE_KIND_PERFMON, MTRACE_PERFMON_START, 0, nullptr, 0);
  if (status != ZX_OK) {
    goto fail;
  }

  active_ = true;
  return ZX_OK;

fail : {
  [[maybe_unused]] zx_status_t status2 =
      mtrace_control_(resource, MTRACE_KIND_PERFMON, MTRACE_PERFMON_FINI, 0, nullptr, 0);
  assert(status2 == ZX_OK);
  return status;
}
}

void PerfmonDevice::PmuStop() {
  zxlogf(DEBUG, "%s called", __func__);

  PmuPerTraceState* per_trace = per_trace_state_.get();
  if (!per_trace) {
    return;
  }

  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  zx_handle_t resource = get_root_resource();
  [[maybe_unused]] zx_status_t status =
      mtrace_control_(resource, MTRACE_KIND_PERFMON, MTRACE_PERFMON_STOP, 0, nullptr, 0);
  assert(status == ZX_OK);
  active_ = false;
  status = mtrace_control_(resource, MTRACE_KIND_PERFMON, MTRACE_PERFMON_FINI, 0, nullptr, 0);
  assert(status == ZX_OK);
}

// Fidl interface.

void PerfmonDevice::GetProperties(GetPropertiesRequestView request,
                                  GetPropertiesCompleter::Sync& completer) {
  FidlPerfmonProperties props{};
  PmuGetProperties(&props);
  completer.Reply(std::move(props));
}

void PerfmonDevice::Initialize(InitializeRequestView request,
                               InitializeCompleter::Sync& completer) {
  zx_status_t status = PmuInitialize(&request->allocation);
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void PerfmonDevice::Terminate(TerminateRequestView request, TerminateCompleter::Sync& completer) {
  PmuTerminate();
  completer.Reply();
}

void PerfmonDevice::GetAllocation(GetAllocationRequestView request,
                                  GetAllocationCompleter::Sync& completer) {
  FidlPerfmonAllocation alloc{};
  zx_status_t status = PmuGetAllocation(&alloc);
  completer.Reply(status != ZX_OK ? nullptr
                                  : fidl::ObjectView<FidlPerfmonAllocation>::FromExternal(&alloc));
}

void PerfmonDevice::StageConfig(StageConfigRequestView request,
                                StageConfigCompleter::Sync& completer) {
  zx_status_t status = PmuStageConfig(&request->config);
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void PerfmonDevice::GetConfig(GetConfigRequestView request, GetConfigCompleter::Sync& completer) {
  FidlPerfmonConfig config{};
  zx_status_t status = PmuGetConfig(&config);
  completer.Reply(status != ZX_OK ? nullptr
                                  : fidl::ObjectView<FidlPerfmonConfig>::FromExternal(&config));
}

void PerfmonDevice::GetBufferHandle(GetBufferHandleRequestView request,
                                    GetBufferHandleCompleter::Sync& completer) {
  zx_handle_t handle;
  zx_status_t status = PmuGetBufferHandle(request->descriptor, &handle);
  completer.Reply(zx::vmo(status != ZX_OK ? ZX_HANDLE_INVALID : handle));
}

void PerfmonDevice::Start(StartRequestView request, StartCompleter::Sync& completer) {
  zx_status_t status = PmuStart();
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void PerfmonDevice::Stop(StopRequestView request, StopCompleter::Sync& completer) {
  PmuStop();
  completer.Reply();
}

// Devhost interface.

zx_status_t PerfmonDevice::DdkOpen(zx_device_t** dev_out, uint32_t flags) {
  if (opened_) {
    return ZX_ERR_ALREADY_BOUND;
  }

  opened_ = true;
  return ZX_OK;
}

zx_status_t PerfmonDevice::DdkClose(uint32_t flags) {
  opened_ = false;
  return ZX_OK;
}

void PerfmonDevice::DdkRelease() {
  PmuStop();
  PmuTerminate();

  delete this;
}

}  // namespace perfmon

zx_status_t perfmon_bind(void* ctx, zx_device_t* parent) {
  zx_status_t status = perfmon::PerfmonDevice::InitOnce();
  if (status != ZX_OK) {
    return status;
  }

  perfmon::PmuHwProperties props;
  status = perfmon::PerfmonDevice::GetHwProperties(zx_mtrace_control, &props);
  if (status != ZX_OK) {
    return status;
  }
  DumpHwProperties(props);

  if (props.common.pm_version < perfmon::kMinPmVersion) {
    zxlogf(INFO, "%s: PM version %u or above is required", __func__, perfmon::kMinPmVersion);
    return ZX_ERR_NOT_SUPPORTED;
  }

  pdev_protocol_t pdev;
  status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev);
  if (status != ZX_OK) {
    return status;
  }

  zx::bti bti;
  status = pdev_get_bti(&pdev, 0, bti.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }

  fbl::AllocChecker ac;
  auto dev = std::unique_ptr<perfmon::PerfmonDevice>(
      new (&ac) perfmon::PerfmonDevice(parent, std::move(bti), props, zx_mtrace_control));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  status = dev->DdkAdd("perfmon");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: could not add device: %d", __func__, status);
  } else {
    // devmgr owns the memory now
    __UNUSED auto ptr = dev.release();
  }
  return status;
}
