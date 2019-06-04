// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// See the README.md in this directory for documentation.

#include <assert.h>
#include <stddef.h>
#include <limits>
#include <memory>
#include <stdint.h>
#include <stdlib.h>

#include <ddk/debug.h>
#include <ddk/protocol/platform/device.h>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

#include <lib/zircon-internal/device/cpu-trace/perf-mon.h>
#include <lib/zircon-internal/mtrace.h>
#include <zircon/syscalls.h>

#include "cpu-trace-private.h"
#include "perf-mon.h"

namespace perfmon {

PmuHwProperties PerfmonDevice::pmu_hw_properties_;

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

zx_status_t BuildEventMap(const EventDetails* events, size_t count,
                          const uint16_t** out_event_map, size_t* out_map_size) {
    static_assert(kMaxEvent < std::numeric_limits<uint16_t>::max());

    uint16_t largest_event_id = GetLargestEventId(events, count);
    // See perf-mon.h: The full event id is split into two pieces:
    // group type and id within that group. The event recorded in
    // |EventDetails| is the id within the group. Each id must be in
    // the range [1,PERFMON_MAX_EVENT]. ID 0 is reserved.
    if (largest_event_id == 0 || largest_event_id > kMaxEvent) {
        zxlogf(ERROR, "PMU: Corrupt event database\n");
        return ZX_ERR_INTERNAL;
    }

    fbl::AllocChecker ac;
    size_t event_map_size = largest_event_id + 1;
    zxlogf(INFO, "PMU: %zu arch events\n", count);
    zxlogf(INFO, "PMU: arch event id range: 1-%zu\n", event_map_size);
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

zx_status_t PerfmonDevice::GetHwProperties() {
    PmuHwProperties props;
    // Please do not use get_root_resource() in new code. See ZX-1467.
    zx_handle_t resource = get_root_resource();
    zx_status_t status =
        zx_mtrace_control(resource, MTRACE_KIND_PERFMON, MTRACE_PERFMON_GET_PROPERTIES,
                          0, &props, sizeof(props));
    if (status != ZX_OK) {
        if (status == ZX_ERR_NOT_SUPPORTED) {
            zxlogf(INFO, "%s: No PM support\n", __func__);
        } else {
            zxlogf(INFO, "%s: Error %d fetching ipm properties\n",
                   __func__, status);
        }
        return status;
    }

    pmu_hw_properties_ = props;
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
    zxlogf(TRACE, "%s called\n", __func__);

    props->api_version = fuchsia_perfmon_cpu_API_VERSION;
    props->pm_version = pmu_hw_properties_.pm_version;
    static_assert(perfmon::kMaxNumEvents == fuchsia_perfmon_cpu_MAX_NUM_EVENTS);
    props->max_num_events = fuchsia_perfmon_cpu_MAX_NUM_EVENTS;

    // These numbers are for informational/debug purposes. There can be
    // further restrictions and limitations.
    // TODO(dje): Something more elaborate can wait for publishing them via
    // some namespace.
    props->max_num_fixed_events = pmu_hw_properties_.max_num_fixed_events;
    props->max_fixed_counter_width = pmu_hw_properties_.max_fixed_counter_width;
    props->max_num_programmable_events = pmu_hw_properties_.max_num_programmable_events;
    props->max_programmable_counter_width = pmu_hw_properties_.max_programmable_counter_width;
    props->max_num_misc_events = pmu_hw_properties_.max_num_misc_events;
    props->max_misc_counter_width = pmu_hw_properties_.max_misc_counter_width;

    props->flags = 0;
#ifdef __x86_64__
    if (pmu_hw_properties_.lbr_stack_size > 0) {
        props->flags |= fuchsia_perfmon_cpu_PropertyFlags_HAS_LAST_BRANCH;
    }
#endif
}

zx_status_t PerfmonDevice::PmuInitialize(const FidlPerfmonAllocation* allocation) {
    zxlogf(TRACE, "%s called\n", __func__);

    if (per_trace_state_) {
        return ZX_ERR_BAD_STATE;
    }

    uint32_t num_cpus = zx_system_get_num_cpus();
    if (allocation->num_buffers != num_cpus) { // TODO(dje): for now
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
    for ( ; i < num_cpus; ++i) {
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
    zxlogf(TRACE, "%s called\n", __func__);

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
    zxlogf(TRACE, "%s called\n", __func__);

    const PmuPerTraceState* per_trace = per_trace_state_.get();
    if (!per_trace) {
        return ZX_ERR_BAD_STATE;
    }

    allocation->num_buffers = per_trace->num_buffers;
    allocation->buffer_size_in_pages = per_trace->buffer_size_in_pages;
    return ZX_OK;
}

zx_status_t PerfmonDevice::PmuGetBufferHandle(uint32_t descriptor,
                                              zx_handle_t* out_handle) {
    zxlogf(TRACE, "%s called\n", __func__);

    const PmuPerTraceState* per_trace = per_trace_state_.get();
    if (!per_trace) {
        return ZX_ERR_BAD_STATE;
    }

    if (descriptor >= per_trace->num_buffers)  {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_handle_t h;
    zx_status_t status =
        zx_handle_duplicate(per_trace->buffers[descriptor].vmo_handle,
                            ZX_RIGHT_SAME_RIGHTS, &h);
    if (status != ZX_OK) {
        // This failure could be hard to debug. Give the user some help.
        zxlogf(ERROR, "%s: Failed to duplicate %u buffer handle: %d\n",
               __func__, descriptor, status);
        return status;
    }

    *out_handle = h;
    return ZX_OK;
}

// Do an architecture-independent verification pass over |icfg|,
// and see if there's a timebase event.
static zx_status_t VerifyAndCheckTimebase(const FidlPerfmonConfig* icfg,
                                          PmuConfig* ocfg) {
    unsigned ii;  // ii: input index
    for (ii = 0; ii < countof(icfg->events); ++ii) {
        EventId id = icfg->events[ii].event;
        if (id == kEventIdNone) {
            break;
        }
        EventRate rate = icfg->events[ii].rate;
        uint32_t flags = icfg->events[ii].flags;

        if (flags & fuchsia_perfmon_cpu_EventConfigFlags_IS_TIMEBASE) {
            if (ocfg->timebase_event != kEventIdNone) {
                zxlogf(ERROR, "%s: multiple timebases [%u]\n", __func__, ii);
                return ZX_ERR_INVALID_ARGS;
            }
            ocfg->timebase_event = icfg->events[ii].event;
        }

        if (flags & fuchsia_perfmon_cpu_EventConfigFlags_COLLECT_PC) {
            if (rate == 0) {
                zxlogf(ERROR, "%s: PC flag requires own timebase, event [%u]\n",
                       __func__, ii);
                return ZX_ERR_INVALID_ARGS;
            }
        }

        if (flags & fuchsia_perfmon_cpu_EventConfigFlags_COLLECT_LAST_BRANCH) {
            // Further verification is architecture specific.
            if (icfg->events[ii].rate == 0) {
                zxlogf(ERROR, "%s: Last branch requires own timebase, event [%u]\n",
                       __func__, ii);
                return ZX_ERR_INVALID_ARGS;
            }
        }
    }

    if (ii == 0) {
        zxlogf(ERROR, "%s: No events provided\n", __func__);
        return ZX_ERR_INVALID_ARGS;
    }

    // Ensure there are no holes.
    for (; ii < countof(icfg->events); ++ii) {
        if (icfg->events[ii].event != kEventIdNone) {
            zxlogf(ERROR, "%s: Hole at event [%u]\n", __func__, ii);
            return ZX_ERR_INVALID_ARGS;
        }
        if (icfg->events[ii].rate != 0) {
            zxlogf(ERROR, "%s: Hole at rate [%u]\n", __func__, ii);
            return ZX_ERR_INVALID_ARGS;
        }
        if (icfg->events[ii].flags != 0) {
            zxlogf(ERROR, "%s: Hole at flags [%u]\n", __func__, ii);
            return ZX_ERR_INVALID_ARGS;
        }
    }

    return ZX_OK;
}

zx_status_t PerfmonDevice::PmuStageConfig(const FidlPerfmonConfig* fidl_config) {
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
        return ZX_OK;
    }

    unsigned ii;  // ii: input index
    for (ii = 0; ii < fbl::count_of(icfg->events); ++ii) {
        EventId id = icfg->events[ii].event;
        zxlogf(TRACE, "%s: processing [%u] = %u\n", __func__, ii, id);
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
            zxlogf(ERROR, "%s: Invalid event [%u] (bad group)\n",
                   __func__, ii);
            return ZX_ERR_INVALID_ARGS;
        }
    }

    // TODO(dje): Basic sanity check that some data will be collected.

    per_trace->fidl_config = *icfg;
    per_trace->configured = true;
    return ZX_OK;
}

zx_status_t PerfmonDevice::PmuGetConfig(FidlPerfmonConfig* config) {
    zxlogf(TRACE, "%s called\n", __func__);

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
    zxlogf(TRACE, "%s called\n", __func__);

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
    zxlogf(TRACE, "%s: global ctrl 0x%lx, fixed ctrl 0x%lx\n",
           __func__, per_trace->config.global_ctrl,
           per_trace->config.fixed_ctrl);

    // |per_trace->configured| should not have been set if there's nothing
    // to trace.
    assert(per_trace->config.global_ctrl != 0);
#endif

    // Please do not use get_root_resource() in new code. See ZX-1467.
    zx_handle_t resource = get_root_resource();

    zx_status_t status =
        zx_mtrace_control(resource, MTRACE_KIND_PERFMON,
                          MTRACE_PERFMON_INIT, 0, nullptr, 0);
    if (status != ZX_OK) {
        return status;
    }

    uint32_t num_cpus = zx_system_get_num_cpus();
    for (uint32_t cpu = 0; cpu < num_cpus; ++cpu) {
        zx_pmu_buffer_t buffer;
        io_buffer_t* io_buffer = &per_trace->buffers[cpu];
        buffer.vmo = io_buffer->vmo_handle;
        status = zx_mtrace_control(resource, MTRACE_KIND_PERFMON,
                                   MTRACE_PERFMON_ASSIGN_BUFFER, cpu,
                                   &buffer, sizeof(buffer));
        if (status != ZX_OK) {
            goto fail;
        }
    }

    status = zx_mtrace_control(resource, MTRACE_KIND_PERFMON,
                               MTRACE_PERFMON_STAGE_CONFIG, 0,
                               &per_trace->config, sizeof(per_trace->config));
    if (status != ZX_OK) {
        goto fail;
    }

    // Step 2: Start data collection.

    status = zx_mtrace_control(resource, MTRACE_KIND_PERFMON, MTRACE_PERFMON_START,
                               0, nullptr, 0);
    if (status != ZX_OK) {
        goto fail;
    }

    active_ = true;
    return ZX_OK;

  fail:
    {
        [[maybe_unused]] zx_status_t status2 =
            zx_mtrace_control(resource, MTRACE_KIND_PERFMON,
                              MTRACE_PERFMON_FINI, 0, nullptr, 0);
        assert(status2 == ZX_OK);
        return status;
    }
}

void PerfmonDevice::PmuStop() {
    zxlogf(TRACE, "%s called\n", __func__);

    PmuPerTraceState* per_trace = per_trace_state_.get();
    if (!per_trace) {
        return;
    }

    // Please do not use get_root_resource() in new code. See ZX-1467.
    zx_handle_t resource = get_root_resource();
    [[maybe_unused]] zx_status_t status =
        zx_mtrace_control(resource, MTRACE_KIND_PERFMON,
                          MTRACE_PERFMON_STOP, 0, nullptr, 0);
    assert(status == ZX_OK);
    active_ = false;
    status = zx_mtrace_control(resource, MTRACE_KIND_PERFMON,
                               MTRACE_PERFMON_FINI, 0, nullptr, 0);
    assert(status == ZX_OK);
}

// Fidl interface.

static zx_status_t fidl_GetProperties(void* ctx, fidl_txn_t* txn) {
    auto dev = reinterpret_cast<PerfmonDevice*>(ctx);
    FidlPerfmonProperties props{};
    dev->PmuGetProperties(&props);
    return fuchsia_perfmon_cpu_ControllerGetProperties_reply(txn, &props);
}

static zx_status_t fidl_Initialize(void* ctx,
                                   const FidlPerfmonAllocation* allocation,
                                   fidl_txn_t* txn) {
    auto dev = reinterpret_cast<PerfmonDevice*>(ctx);
    zx_status_t status = dev->PmuInitialize(allocation);
    fuchsia_perfmon_cpu_Controller_Initialize_Result result{};
    if (status == ZX_OK) {
        result.tag = fuchsia_perfmon_cpu_Controller_Initialize_ResultTag_response;
    } else {
        result.tag = fuchsia_perfmon_cpu_Controller_Initialize_ResultTag_err;
        result.err = status;
    }
    return fuchsia_perfmon_cpu_ControllerInitialize_reply(txn, &result);
}

static zx_status_t fidl_Terminate(void* ctx, fidl_txn_t* txn) {
    auto dev = reinterpret_cast<PerfmonDevice*>(ctx);
    dev->PmuTerminate();
    return fuchsia_perfmon_cpu_ControllerTerminate_reply(txn);
}

static zx_status_t fidl_GetAllocation(void* ctx, fidl_txn_t* txn) {
    auto dev = reinterpret_cast<PerfmonDevice*>(ctx);
    FidlPerfmonAllocation alloc{};
    zx_status_t status = dev->PmuGetAllocation(&alloc);
    if (status != ZX_OK) {
        return fuchsia_perfmon_cpu_ControllerGetAllocation_reply(
            txn, nullptr);
    }
    return fuchsia_perfmon_cpu_ControllerGetAllocation_reply(txn, &alloc);
}

static zx_status_t fidl_StageConfig(void* ctx, const FidlPerfmonConfig* config,
                                    fidl_txn_t* txn) {
    auto dev = reinterpret_cast<PerfmonDevice*>(ctx);
    zx_status_t status = dev->PmuStageConfig(config);
    fuchsia_perfmon_cpu_Controller_StageConfig_Result result{};
    if (status == ZX_OK) {
        result.tag = fuchsia_perfmon_cpu_Controller_StageConfig_ResultTag_response;
    } else {
        result.tag = fuchsia_perfmon_cpu_Controller_StageConfig_ResultTag_err;
        result.err = status;
    }
    return fuchsia_perfmon_cpu_ControllerStageConfig_reply(txn, &result);
}

static zx_status_t fidl_GetConfig(void* ctx, fidl_txn_t* txn) {
    auto dev = reinterpret_cast<PerfmonDevice*>(ctx);
    FidlPerfmonConfig config{};
    zx_status_t status = dev->PmuGetConfig(&config);
    if (status != ZX_OK) {
        return fuchsia_perfmon_cpu_ControllerGetConfig_reply(txn, nullptr);
    }
    return fuchsia_perfmon_cpu_ControllerGetConfig_reply(txn, &config);
}

static zx_status_t fidl_GetBufferHandle(void* ctx, uint32_t descriptor, fidl_txn_t* txn) {
    auto dev = reinterpret_cast<PerfmonDevice*>(ctx);
    zx_handle_t handle;
    zx_status_t status = dev->PmuGetBufferHandle(descriptor, &handle);
    if (status != ZX_OK) {
        return fuchsia_perfmon_cpu_ControllerGetBufferHandle_reply(
            txn, ZX_HANDLE_INVALID);
    }
    return fuchsia_perfmon_cpu_ControllerGetBufferHandle_reply(txn, handle);
}

static zx_status_t fidl_Start(void* ctx, fidl_txn_t* txn) {
    auto dev = reinterpret_cast<PerfmonDevice*>(ctx);
    zx_status_t status = dev->PmuStart();
    fuchsia_perfmon_cpu_Controller_Start_Result result{};
    if (status == ZX_OK) {
        result.tag = fuchsia_perfmon_cpu_Controller_Start_ResultTag_response;
    } else {
        result.tag = fuchsia_perfmon_cpu_Controller_Start_ResultTag_err;
        result.err = status;
    }
    return fuchsia_perfmon_cpu_ControllerStart_reply(txn, &result);
}

static zx_status_t fidl_Stop(void* ctx, fidl_txn_t* txn) {
    auto dev = reinterpret_cast<PerfmonDevice*>(ctx);
    dev->PmuStop();
    return fuchsia_perfmon_cpu_ControllerStop_reply(txn);
}

static const fuchsia_perfmon_cpu_Controller_ops_t fidl_ops = {
    .GetProperties = fidl_GetProperties,
    .Initialize = fidl_Initialize,
    .Terminate = fidl_Terminate,
    .GetAllocation = fidl_GetAllocation,
    .StageConfig = fidl_StageConfig,
    .GetConfig = fidl_GetConfig,
    .GetBufferHandle = fidl_GetBufferHandle,
    .Start = fidl_Start,
    .Stop = fidl_Stop,
};


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

zx_status_t PerfmonDevice::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    mtx_lock(&lock_);
    zx_status_t status =
        fuchsia_perfmon_cpu_Controller_dispatch(this, txn, msg, &fidl_ops);
    mtx_unlock(&lock_);

    return status;
}

void PerfmonDevice::DdkRelease() {
    PmuStop();
    PmuTerminate();

    delete this;
}

} // namespace perfmon

zx_status_t perfmon_bind(void* ctx, zx_device_t* parent) {
    zx_status_t status = perfmon::PerfmonDevice::InitOnce();
    if (status != ZX_OK) {
        return status;
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
        new (&ac) perfmon::PerfmonDevice(parent, std::move(bti)));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    status = dev->DdkAdd("perfmon");
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: could not add device: %d\n", __func__, status);
    } else {
        // devmgr owns the memory now
        __UNUSED auto ptr = dev.release();
    }
    return status;
}
