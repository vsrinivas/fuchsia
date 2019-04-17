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
    auto a = reinterpret_cast<const perfmon_event_id_t*>(ap);
    auto b = reinterpret_cast<const perfmon_event_id_t*>(bp);
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
    static_assert(PERFMON_MAX_EVENT < std::numeric_limits<uint16_t>::max());

    uint16_t largest_event_id = GetLargestEventId(events, count);
    // See perf-mon.h: The full event id is split into two pieces:
    // group type and id within that group. The event recorded in
    // |EventDetails| is the id within the group. Each id must be in
    // the range [1,PERFMON_MAX_EVENT]. ID 0 is reserved.
    if (largest_event_id == 0 || largest_event_id > PERFMON_MAX_EVENT) {
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
    // Please do not use get_root_resource() in new code. See ZX-1497.
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

zx_status_t PerfmonDevice::PmuGetProperties(void* reply, size_t replymax,
                                            size_t* out_actual) {
    zxlogf(TRACE, "%s called\n", __func__);

    perfmon_properties_t props{};
    if (replymax < sizeof(props)) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }

    props.api_version = PERFMON_API_VERSION;
    // TODO(dje): Remove cast in subsequent patch.
    props.pm_version = pmu_hw_properties_.pm_version;
    // To the arch-independent API, the misc events on Intel are currently
    // all "fixed" in the sense that they don't occupy a limited number of
    // programmable slots. Ultimately there could still be limitations (e.g.,
    // some combination of events can't be supported) but that's ok. This
    // data is for informational/debug purposes.
    // TODO(dje): Something more elaborate can wait for publishing them via
    // some namespace.
    props.num_fixed_events = static_cast<uint16_t>(
        pmu_hw_properties_.num_fixed_events + pmu_hw_properties_.num_misc_events);
    props.num_programmable_events = pmu_hw_properties_.num_programmable_events;
    props.fixed_counter_width = pmu_hw_properties_.fixed_counter_width;
    props.programmable_counter_width = pmu_hw_properties_.programmable_counter_width;
#ifdef __x86_64__
    if (pmu_hw_properties_.lbr_stack_size > 0) {
        props.flags |= PERFMON_PROPERTY_FLAG_HAS_LAST_BRANCH;
    }
#endif

    memcpy(reply, &props, sizeof(props));
    *out_actual = sizeof(props);
    return ZX_OK;
}

zx_status_t PerfmonDevice::PmuAllocTrace(const void* cmd, size_t cmdlen) {
    zxlogf(TRACE, "%s called\n", __func__);

    if (per_trace_state_) {
        return ZX_ERR_BAD_STATE;
    }

    ioctl_perfmon_alloc_t alloc;
    if (cmdlen != sizeof(alloc)) {
        return ZX_ERR_INVALID_ARGS;
    }
    memcpy(&alloc, cmd, sizeof(alloc));
    if (alloc.buffer_size > kMaxPerTraceSpace) {
        return ZX_ERR_INVALID_ARGS;
    }
    uint32_t num_cpus = zx_system_get_num_cpus();
    if (alloc.num_buffers != num_cpus) { // TODO(dje): for now
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

    uint32_t i = 0;
    for ( ; i < num_cpus; ++i) {
        zx_status_t status =
            io_buffer_init(&per_trace->buffers[i], bti_.get(), alloc.buffer_size, IO_BUFFER_RW);
        if (status != ZX_OK) {
            break;
        }
    }
    if (i != num_cpus) {
        FreeBuffersForTrace(per_trace.get(), i);
        return ZX_ERR_NO_MEMORY;
    }

    per_trace->num_buffers = alloc.num_buffers;
    per_trace->buffer_size = alloc.buffer_size;
    per_trace_state_ = std::move(per_trace);
    return ZX_OK;
}

zx_status_t PerfmonDevice::PmuFreeTrace() {
    zxlogf(TRACE, "%s called\n", __func__);

    if (active_) {
        return ZX_ERR_BAD_STATE;
    }
    PmuPerTraceState* per_trace = per_trace_state_.get();
    if (!per_trace) {
        return ZX_ERR_BAD_STATE;
    }

    FreeBuffersForTrace(per_trace, per_trace->num_buffers);
    per_trace_state_.reset();
    return ZX_OK;
}

zx_status_t PerfmonDevice::PmuGetAlloc(void* reply, size_t replymax,
                                       size_t* out_actual) {
    zxlogf(TRACE, "%s called\n", __func__);

    const PmuPerTraceState* per_trace = per_trace_state_.get();
    if (!per_trace) {
        return ZX_ERR_BAD_STATE;
    }

    ioctl_perfmon_alloc_t alloc{};
    if (replymax < sizeof(alloc)) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }

    alloc.num_buffers = per_trace->num_buffers;
    alloc.buffer_size = per_trace->buffer_size;
    memcpy(reply, &alloc, sizeof(alloc));
    *out_actual = sizeof(alloc);
    return ZX_OK;
}

zx_status_t PerfmonDevice::PmuGetBufferHandle(const void* cmd, size_t cmdlen,
                                              void* reply, size_t replymax,
                                              size_t* out_actual) {
    zxlogf(TRACE, "%s called\n", __func__);

    const PmuPerTraceState* per_trace = per_trace_state_.get();
    if (!per_trace) {
        return ZX_ERR_BAD_STATE;
    }

    ioctl_perfmon_buffer_handle_req_t req;
    zx_handle_t h;

    if (cmdlen != sizeof(req)) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (replymax < sizeof(h)) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }
    memcpy(&req, cmd, sizeof(req));
    if (req.descriptor >= per_trace->num_buffers) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_status_t status = zx_handle_duplicate(per_trace->buffers[req.descriptor].vmo_handle, ZX_RIGHT_SAME_RIGHTS, &h);
    if (status < 0) {
        return status;
    }
    memcpy(reply, &h, sizeof(h));
    *out_actual = sizeof(h);
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

    perfmon_config_t ioctl_config;
    perfmon_config_t* icfg = &ioctl_config;
    if (cmdlen != sizeof(*icfg)) {
        return ZX_ERR_INVALID_ARGS;
    }
    memcpy(icfg, cmd, sizeof(*icfg));

    PmuConfig* ocfg = &per_trace->config;
    *ocfg = {};

    // Validate the config and convert it to our internal form.
    // TODO(dje): Multiplexing support.

    StagingState staging_state{};
    StagingState* ss = &staging_state;
    InitializeStagingState(ss);

    zx_status_t status;
    unsigned ii;  // ii: input index
    for (ii = 0; ii < fbl::count_of(icfg->events); ++ii) {
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
            status = StageFixedConfig(icfg, ss, ii, ocfg);
            if (status != ZX_OK) {
                return status;
            }
            break;
        case PERFMON_GROUP_ARCH:
        case PERFMON_GROUP_MODEL:
            status = StageProgrammableConfig(icfg, ss, ii, ocfg);
            if (status != ZX_OK) {
                return status;
            }
            break;
        case PERFMON_GROUP_MISC:
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

        if (icfg->flags[ii] & PERFMON_CONFIG_FLAG_TIMEBASE0) {
            ss->have_timebase0_user = true;
        }
    }
    if (ii == 0) {
        zxlogf(ERROR, "%s: No events provided\n", __func__);
        return ZX_ERR_INVALID_ARGS;
    }

    // Ensure there are no holes.
    for (; ii < fbl::count_of(icfg->events); ++ii) {
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

    per_trace->ioctl_config = *icfg;
    per_trace->configured = true;
    return ZX_OK;
}

zx_status_t PerfmonDevice::PmuGetConfig(void* reply, size_t replymax,
                                        size_t* out_actual) {
    zxlogf(TRACE, "%s called\n", __func__);

    const PmuPerTraceState* per_trace = per_trace_state_.get();
    if (!per_trace) {
        return ZX_ERR_BAD_STATE;
    }

    if (!per_trace->configured) {
        return ZX_ERR_BAD_STATE;
    }

    const perfmon_config_t* config = &per_trace->ioctl_config;
    if (replymax < sizeof(*config)) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }

    memcpy(reply, config, sizeof(*config));
    *out_actual = sizeof(*config);
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

    // Please do not use get_root_resource() in new code. See ZX-1497.
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
        zx_status_t status2 =
            zx_mtrace_control(resource, MTRACE_KIND_PERFMON,
                              MTRACE_PERFMON_FINI, 0, nullptr, 0);
        if (status2 != ZX_OK) {
            zxlogf(TRACE, "%s: MTRACE_PERFMON_FINI failed: %d\n", __func__, status2);
        }
        assert(status2 == ZX_OK);
        return status;
    }
}

zx_status_t PerfmonDevice::PmuStop() {
    zxlogf(TRACE, "%s called\n", __func__);

    PmuPerTraceState* per_trace = per_trace_state_.get();
    if (!per_trace) {
        return ZX_ERR_BAD_STATE;
    }

    // Please do not use get_root_resource() in new code. See ZX-1497.
    zx_handle_t resource = get_root_resource();
    zx_status_t status =
        zx_mtrace_control(resource, MTRACE_KIND_PERFMON,
                          MTRACE_PERFMON_STOP, 0, nullptr, 0);
    if (status == ZX_OK) {
        active_ = false;
        status = zx_mtrace_control(resource, MTRACE_KIND_PERFMON,
                                   MTRACE_PERFMON_FINI, 0, nullptr, 0);
    }
    return status;
}

// Dispatch the various kinds of requests.

zx_status_t PerfmonDevice::IoctlWorker(uint32_t op,
                                       const void* cmd, size_t cmdlen,
                                       void* reply, size_t replymax,
                                       size_t* out_actual) {
    assert(IOCTL_FAMILY(op) == IOCTL_FAMILY_PERFMON);

    switch (op) {
    case IOCTL_PERFMON_GET_PROPERTIES:
        if (cmdlen != 0) {
            return ZX_ERR_INVALID_ARGS;
        }
        return PmuGetProperties(reply, replymax, out_actual);

    case IOCTL_PERFMON_ALLOC_TRACE:
        if (replymax != 0) {
            return ZX_ERR_INVALID_ARGS;
        }
        return PmuAllocTrace(cmd, cmdlen);

    case IOCTL_PERFMON_FREE_TRACE:
        if (cmdlen != 0 || replymax != 0) {
            return ZX_ERR_INVALID_ARGS;
        }
        return PmuFreeTrace();

    case IOCTL_PERFMON_GET_ALLOC:
        if (cmdlen != 0) {
            return ZX_ERR_INVALID_ARGS;
        }
        return PmuGetAlloc(reply, replymax, out_actual);

    case IOCTL_PERFMON_GET_BUFFER_HANDLE:
        return PmuGetBufferHandle(cmd, cmdlen, reply, replymax, out_actual);

    case IOCTL_PERFMON_STAGE_CONFIG:
        if (replymax != 0) {
            return ZX_ERR_INVALID_ARGS;
        }
        return PmuStageConfig(cmd, cmdlen);

    case IOCTL_PERFMON_GET_CONFIG:
        return PmuGetConfig(reply, replymax, out_actual);

    case IOCTL_PERFMON_START:
        if (cmdlen != 0 || replymax != 0) {
            return ZX_ERR_INVALID_ARGS;
        }
        return PmuStart();

    case IOCTL_PERFMON_STOP:
        if (cmdlen != 0 || replymax != 0) {
            return ZX_ERR_INVALID_ARGS;
        }
        return PmuStop();

    default:
        return ZX_ERR_INVALID_ARGS;
    }
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

zx_status_t PerfmonDevice::DdkIoctl(uint32_t op, const void* cmd, size_t cmdlen,
                                    void* reply, size_t replymax,
                                    size_t* out_actual) {
    mtx_lock(&lock_);

    ssize_t result;
    switch (IOCTL_FAMILY(op)) {
        case IOCTL_FAMILY_PERFMON:
            result = IoctlWorker(op, cmd, cmdlen,
                                 reply, replymax, out_actual);
            break;
        default:
            result = ZX_ERR_INVALID_ARGS;
            break;
    }

    mtx_unlock(&lock_);

    return static_cast<zx_status_t>(result);
}

void PerfmonDevice::DdkRelease() {
    // TODO(dje): None of these should fail. What to do?
    // Suggest flagging things as busted and prevent further use.
    PmuStop();
    PmuFreeTrace();

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
