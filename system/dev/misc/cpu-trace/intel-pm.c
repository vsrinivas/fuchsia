// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// See the README.md in this directory for documentation.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/io-buffer.h>

#include <zircon/device/intel-pm.h>
#include <zircon/mtrace.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/resource.h>
#include <zircon/types.h>

#include <assert.h>
#include <cpuid.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu-trace-private.h"

// TODO(dje): Having trouble getting this working, so just punt for now.
#define TRY_FREEZE_ON_PMI 0

// Individual bits in the fixed counter enable field.
// See Intel Volume 3, Figure 18-2 "Layout of IA32_FIXED_CTR_CTRL MSR".
#define FIXED_CTR_ENABLE_OS 1
#define FIXED_CTR_ENABLE_USR 2

typedef enum {
// N.B. The order of fixed/arch/nonarch here must match |perf_events|.
#define DEF_ARCH_EVENT(symbol, ebx_bit, event, umask, flags, name) \
  symbol,
#define DEF_SKL_EVENT(symbol, event, umask, flags, name) \
  symbol,
#include <zircon/device/intel-pm-events.inc>
} perf_event_kind_t;

typedef struct {
    uint32_t event;
    uint32_t umask;
    uint32_t flags;
} perf_event_t;

static const perf_event_t kPerfEvents[] = {
// N.B. The order of fixed/arch/nonarch here must match perf_event_kind_t.
#define DEF_ARCH_EVENT(symbol, ebx_bit, event, umask, flags, description) \
  { event, umask, flags },
#define DEF_SKL_EVENT(symbol, event, umask, flags, description) \
  { event, umask, flags },
#include <zircon/device/intel-pm-events.inc>
};

// All configuration data is staged here before writing any MSRs, etc.
// Then when ready the "START" ioctl will write all the necessary MSRS,
// and do whatever kernel operations are required for collecting data.

typedef struct ipm_per_trace_state {
    // true if |config| has been set.
    bool configured;

    zx_x86_ipm_perf_config_t config;

    // # of entries in |buffers|.
    // TODO(dje): This is generally the number of cpus, but it could be
    // something else later.
    uint32_t num_buffers;

    // Each buffer is the same size (at least for now, KISS).
    // There is one buffer per cpu.
    // In "counting mode" there is only need for one page of data.
    // In "sampling mode" this contains the accumulated collection of
    // trace records.
    // This is a uint32 instead of uint64 as there's no point in supporting
    // that large of a buffer.
    uint32_t buffer_size;

    io_buffer_t* buffers;
} ipm_per_trace_state_t;

typedef struct ipm_device {
    // Once tracing has started various things are not allowed until it stops.
    bool active;

    // one entry for each trace
    // TODO(dje): At the moment we only support one trace at a time.
    // "trace" == "data collection run"
    ipm_per_trace_state_t* per_trace_state;
} ipm_device_t;

static bool ipm_config_supported = false;
static uint32_t ipm_config_version = 0;
static uint32_t ipm_num_programmable_counters = 0;
static uint32_t ipm_num_fixed_counters = 0;

// maximum space, in bytes, for trace buffers (per cpu)
#define MAX_PER_TRACE_SPACE (256 * 1024 * 1024)

void ipm_init_once(void)
{
    unsigned a, b, c, d, max_leaf;
    const unsigned kCpuidPerfMon = 0xa;

    max_leaf = __get_cpuid_max(0, NULL);
    if (max_leaf < kCpuidPerfMon) {
        zxlogf(INFO, "%s: No PM support\n", __func__);
        return;
    }

    __cpuid_count(kCpuidPerfMon, 0, a, b, c, d);
    ipm_config_version = a & 0xff;
    // Skylake supports version 4. KISS and begin with that.
    // Note: This should agree with the kernel driver's check.
    if (ipm_config_version < 4) {
        zxlogf(INFO, "%s: PM version 4 or above is required\n", __func__);
        return;
    }

    ipm_num_programmable_counters = (a >> 8) & 0xff;
    ipm_num_fixed_counters = d & 0x1f;
    ipm_config_supported = true;

    zxlogf(TRACE, "Intel Performance Monitor configuration for this chipset:\n");
    // No need to print everything, but these are useful.
    zxlogf(TRACE, "IPM: version: %u\n", ipm_config_version);
    zxlogf(TRACE, "IPM: num_programmable_counters: %u\n", ipm_num_programmable_counters);
    zxlogf(TRACE, "IPM: num_fixed_counters: %u\n", ipm_num_fixed_counters);
}


// Category to register values converter
// TODO(dje): It's nice to provide a simpler API for configuring the h/w,
// but one can reasonably argue the client, e.g., apps/tracing, should provide
// it. OTOH, if there are several clients IWBN if they could all share the same
// mechanism (without adding yet another layer). Revisit, later.

typedef struct {
    size_t count;
    const perf_event_kind_t* events;
} category_spec_t;

#define DEF_CATEGORY(symbol, id, name, counters...) \
  static const perf_event_kind_t symbol ## _events[] = { counters };
#include "zircon/device/intel-pm-categories.inc"

static const category_spec_t kCategorySpecs[] = {
#define DEF_CATEGORY(symbol, id, name, counters...) \
  { \
    countof(symbol ## _events), \
    &symbol ## _events[0] \
  },
#include "zircon/device/intel-pm-categories.inc"
};

// Map programmable category ids to indices in |category_specs|.
static const uint32_t kProgrammableCategoryMap[] = {
#define DEF_CATEGORY(symbol, id, name, counters...) \
  [id] = symbol,
#include <zircon/device/intel-pm-categories.inc>
};

static_assert(countof(kCategorySpecs) == IPM_CATEGORY_MAX, "");
static_assert(countof(kProgrammableCategoryMap) <=
              IPM_CATEGORY_PROGRAMMABLE_MAX, "");

static uint32_t get_simple_config_os_usr_mask(const ioctl_ipm_simple_perf_config_t* simple_config) {
    uint32_t os_usr_mask = simple_config->categories & (IPM_CATEGORY_OS | IPM_CATEGORY_USR);
    // TODO(dje): Maybe convert no bits specified -> both os+usr. Later.
    return os_usr_mask;
}

static zx_status_t get_simple_config_sample_freq(
        const ioctl_ipm_simple_perf_config_t* simple_config,
        uint32_t* sample_freq) {
    uint32_t freq_sel = simple_config->categories & IPM_CATEGORY_MODE_MASK;
    switch (freq_sel) {
    case IPM_CATEGORY_TALLY:
        *sample_freq = 0;
        break;
    case IPM_CATEGORY_SAMPLE_1000:
        *sample_freq = 1000;
        break;
    case IPM_CATEGORY_SAMPLE_5000:
        *sample_freq = 5000;
        break;
    case IPM_CATEGORY_SAMPLE_10000:
        *sample_freq = 10000;
        break;
    case IPM_CATEGORY_SAMPLE_50000:
        *sample_freq = 50000;
        break;
    case IPM_CATEGORY_SAMPLE_100000:
        *sample_freq = 100000;
        break;
    case IPM_CATEGORY_SAMPLE_500000:
        *sample_freq = 500000;
        break;
    case IPM_CATEGORY_SAMPLE_1000000:
        *sample_freq = 1000000;
        break;
    default:
        zxlogf(ERROR, "ipm: invalid sample frequency: 0x%x\n", freq_sel);
        return ZX_ERR_INVALID_ARGS;
    }
    return ZX_OK;
}

static zx_status_t fixed_to_config(const ioctl_ipm_simple_perf_config_t* simple_config,
                                   zx_x86_ipm_perf_config_t* config) {
    uint32_t os_usr_mask = get_simple_config_os_usr_mask(simple_config);
    uint32_t enable = 0;
    if (os_usr_mask & IPM_CATEGORY_OS)
        enable |= FIXED_CTR_ENABLE_OS;
    if (os_usr_mask & IPM_CATEGORY_USR)
        enable |= FIXED_CTR_ENABLE_USR;
    // Indexed by fixed counter number.
    static const uint32_t event_mask[] = {
        IPM_CATEGORY_FIXED_CTR0,
        IPM_CATEGORY_FIXED_CTR1,
        IPM_CATEGORY_FIXED_CTR2,
    };
    uint32_t num_fixed = ipm_num_fixed_counters;
    if (countof(event_mask) < num_fixed)
        num_fixed = countof(event_mask);
    for (uint32_t i = 0; i < num_fixed; ++i) {
        if (simple_config->categories & event_mask[i]) {
            config->fixed_counter_ctrl |= enable << IA32_FIXED_CTR_CTRL_EN_SHIFT(i);
            config->global_ctrl |= IA32_PERF_GLOBAL_CTRL_FIXED_EN_MASK(i);
        }
    }

    return ZX_OK; // TODO(dje): Maybe remove, but later.
}

static zx_status_t category_to_config(const ioctl_ipm_simple_perf_config_t* simple_config,
                                      const category_spec_t* spec,
                                      zx_x86_ipm_perf_config_t* config) {
    uint32_t os_usr_mask = get_simple_config_os_usr_mask(simple_config);

    for (size_t i = 0; i < spec->count && i < ipm_num_programmable_counters; ++i) {
        const perf_event_t* event = &kPerfEvents[spec->events[i]];
        uint64_t evtsel = 0;
        evtsel |= event->event << IA32_PERFEVTSEL_EVENT_SELECT_SHIFT;
        evtsel |= event->umask << IA32_PERFEVTSEL_UMASK_SHIFT;
        if (os_usr_mask & IPM_CATEGORY_OS)
            evtsel |= IA32_PERFEVTSEL_OS_MASK;
        if (os_usr_mask & IPM_CATEGORY_USR)
            evtsel |= IA32_PERFEVTSEL_USR_MASK;
        if (event->flags & IPM_REG_FLAG_EDG)
            evtsel |= IA32_PERFEVTSEL_E_MASK;
        if (event->flags & IPM_REG_FLAG_ANYT)
            evtsel |= IA32_PERFEVTSEL_ANY_MASK;
        if (event->flags & IPM_REG_FLAG_INV)
            evtsel |= IA32_PERFEVTSEL_INV_MASK;
        evtsel |= (event->flags & IPM_REG_FLAG_CMSK_MASK) << IA32_PERFEVTSEL_CMASK_SHIFT;
        evtsel |= IA32_PERFEVTSEL_EN_MASK;
        config->programmable_events[i] = evtsel;
        config->global_ctrl |= IA32_PERF_GLOBAL_CTRL_PMC_EN_MASK(i);
    }

    return ZX_OK; // TODO(dje): Maybe remove, but later.
}

static zx_status_t simple_config_to_cpu_config(const ioctl_ipm_simple_perf_config_t* simple_config,
                                               zx_x86_ipm_perf_config_t* config) {
    uint32_t programmable_category =
        simple_config->categories & IPM_CATEGORY_PROGRAMMABLE_MASK;
    bool use_fixed = !!(simple_config->categories & IPM_CATEGORY_FIXED_MASK);
    uint32_t os_usr_mask = get_simple_config_os_usr_mask(simple_config);
    uint32_t sample_freq;
    zx_status_t status;

    status = get_simple_config_sample_freq(simple_config, &sample_freq);
    if (status != ZX_OK)
        return status;

    memset(config, 0, sizeof(*config));

    if (use_fixed) {
        status = fixed_to_config(simple_config, config);
        if (status != ZX_OK)
            return status;
    }

    if (programmable_category >= countof(kProgrammableCategoryMap)) {
        zxlogf(ERROR, "ipm: bad programmable category %u\n", programmable_category);
        return ZX_ERR_INVALID_ARGS;
    }
    if (programmable_category != IPM_CATEGORY_NONE) {
        uint32_t spec_index = kProgrammableCategoryMap[programmable_category];
        status = category_to_config(simple_config,
                                    &kCategorySpecs[spec_index],
                                    config);
        if (status != ZX_OK)
            return status;
    }

    config->sample_freq = sample_freq;

#if TRY_FREEZE_ON_PMI
    config->debug_ctrl = IA32_DEBUGCTL_FREEZE_PERFMON_ON_PMI_MASK;
#endif

    if (!os_usr_mask) {
        // Help the user understand why there's no data.
        zxlogf(INFO, "ipm: Neither OS nor USR tracing specified\n");
    }

    return ZX_OK;
}


// Helper routines for the ioctls.

static void ipm_free_buffers_for_trace(ipm_per_trace_state_t* per_trace, uint32_t num_allocated) {
    // Note: This may be called with partially allocated buffers.
    assert(per_trace->buffers);
    assert(num_allocated <= per_trace->num_buffers);
    for (uint32_t i = 0; i < num_allocated; ++i)
        io_buffer_release(&per_trace->buffers[i]);
    free(per_trace->buffers);
    per_trace->buffers = NULL;
}


// The userspace side of the driver.

static zx_status_t ipm_get_state(cpu_trace_device_t* dev,
                                 void* reply, size_t replymax,
                                 size_t* out_actual) {
    zxlogf(TRACE, "%s called\n", __func__);

    zx_x86_ipm_state_t state;
    if (replymax < sizeof(state))
        return ZX_ERR_BUFFER_TOO_SMALL;

    zx_handle_t resource = get_root_resource();
    zx_status_t status =
        zx_mtrace_control(resource, MTRACE_KIND_IPM, MTRACE_IPM_GET_STATE,
                          0, &state, sizeof(state));
    if (status != ZX_OK)
        return status;

    memcpy(reply, &state, sizeof(state));
    *out_actual = sizeof(state);
    return ZX_OK;
}

static zx_status_t ipm_alloc_trace(cpu_trace_device_t* dev,
                                   const void* cmd, size_t cmdlen) {
    zxlogf(TRACE, "%s called\n", __func__);

    if (!ipm_config_supported)
        return ZX_ERR_NOT_SUPPORTED;

    ioctl_ipm_trace_config_t config;
    if (cmdlen != sizeof(config))
        return ZX_ERR_INVALID_ARGS;
    memcpy(&config, cmd, sizeof(config));
    if (config.buffer_size > MAX_PER_TRACE_SPACE)
        return ZX_ERR_INVALID_ARGS;
    uint32_t num_cpus = zx_system_get_num_cpus();
    if (config.num_buffers != num_cpus) // TODO(dje): for now
        return ZX_ERR_INVALID_ARGS;

    if (dev->ipm)
        return ZX_ERR_BAD_STATE;

    ipm_device_t* ipm = calloc(1, sizeof(*dev->ipm));
    if (!ipm)
        return ZX_ERR_NO_MEMORY;

    ipm_per_trace_state_t* per_trace = calloc(1, sizeof(ipm->per_trace_state[0]));
    if (!per_trace) {
        free(ipm);
        return ZX_ERR_NO_MEMORY;
    }

    per_trace->buffers = calloc(num_cpus, sizeof(per_trace->buffers[0]));
    if (!per_trace->buffers) {
        free(per_trace);
        free(ipm);
        return ZX_ERR_NO_MEMORY;
    }

    uint32_t i = 0;
    for ( ; i < num_cpus; ++i) {
        zx_status_t status =
            io_buffer_init(&per_trace->buffers[i],
                           config.buffer_size, IO_BUFFER_RW);
        if (status != ZX_OK)
            break;
    }
    if (i != num_cpus) {
        ipm_free_buffers_for_trace(per_trace, i);
        free(per_trace);
        free(ipm);
        return ZX_ERR_NO_MEMORY;
    }

    per_trace->num_buffers = config.num_buffers;
    per_trace->buffer_size = config.buffer_size;
    ipm->per_trace_state = per_trace;
    dev->ipm = ipm;
    return ZX_OK;
}

static zx_status_t ipm_free_trace(cpu_trace_device_t* dev) {
    zxlogf(TRACE, "%s called\n", __func__);

    ipm_device_t* ipm = dev->ipm;
    if (!ipm)
        return ZX_ERR_BAD_STATE;
    if (ipm->active)
        return ZX_ERR_BAD_STATE;

    ipm_per_trace_state_t* per_trace = ipm->per_trace_state;
    ipm_free_buffers_for_trace(per_trace, per_trace->num_buffers);
    free(per_trace);
    free(ipm);
    dev->ipm = NULL;
    return ZX_OK;
}

static zx_status_t ipm_get_trace_config(cpu_trace_device_t* dev,
                                        void* reply, size_t replymax,
                                        size_t* out_actual) {
    zxlogf(TRACE, "%s called\n", __func__);

    ipm_device_t* ipm = dev->ipm;
    if (!ipm)
        return ZX_ERR_BAD_STATE;

    ioctl_ipm_trace_config_t config;
    if (replymax < sizeof(config))
        return ZX_ERR_BUFFER_TOO_SMALL;

    config.num_buffers = ipm->per_trace_state->num_buffers;
    config.buffer_size = ipm->per_trace_state->buffer_size;
    memcpy(reply, &config, sizeof(config));
    *out_actual = sizeof(config);
    return ZX_OK;
}

static zx_status_t ipm_get_buffer_info(cpu_trace_device_t* dev,
                                       const void* cmd, size_t cmdlen,
                                       void* reply, size_t replymax,
                                       size_t* out_actual) {
    zxlogf(TRACE, "%s called\n", __func__);

    ipm_device_t* ipm = dev->ipm;
    if (!ipm)
        return ZX_ERR_BAD_STATE;

    uint32_t index;
    if (cmdlen != sizeof(index))
        return ZX_ERR_INVALID_ARGS;
    memcpy(&index, cmd, sizeof(index));
    const ipm_per_trace_state_t* per_trace = ipm->per_trace_state;
    if (index >= per_trace->num_buffers)
        return ZX_ERR_INVALID_ARGS;

    ioctl_ipm_buffer_info_t info;
    if (replymax < sizeof(info))
        return ZX_ERR_BUFFER_TOO_SMALL;

    if (ipm->active)
        return ZX_ERR_BAD_STATE;

    memcpy(reply, &info, sizeof(info));
    *out_actual = sizeof(info);
    return ZX_OK;
}

static zx_status_t ipm_get_buffer_handle(cpu_trace_device_t* dev,
                                         const void* cmd, size_t cmdlen,
                                         void* reply, size_t replymax,
                                         size_t* out_actual) {
    zxlogf(TRACE, "%s called\n", __func__);

    ipm_device_t* ipm = dev->ipm;
    if (!ipm)
        return ZX_ERR_BAD_STATE;

    ioctl_ipm_buffer_handle_req_t req;
    zx_handle_t h;

    if (cmdlen != sizeof(req))
        return ZX_ERR_INVALID_ARGS;
    if (replymax < sizeof(h))
        return ZX_ERR_BUFFER_TOO_SMALL;
    const ipm_per_trace_state_t* per_trace = ipm->per_trace_state;
    memcpy(&req, cmd, sizeof(req));
    if (req.descriptor >= per_trace->num_buffers)
        return ZX_ERR_INVALID_ARGS;

    zx_status_t status = zx_handle_duplicate(per_trace->buffers[req.descriptor].vmo_handle, ZX_RIGHT_SAME_RIGHTS, &h);
    if (status < 0)
        return status;
    memcpy(reply, &h, sizeof(h));
    *out_actual = sizeof(h);
    return ZX_OK;
}

static zx_status_t ipm_stage_perf_config(cpu_trace_device_t* dev,
                                         const void* cmd, size_t cmdlen) {
    zxlogf(TRACE, "%s called\n", __func__);

    ipm_device_t* ipm = dev->ipm;
    if (!ipm)
        return ZX_ERR_BAD_STATE;

    ioctl_ipm_perf_config_t config;
    if (cmdlen != sizeof(config))
        return ZX_ERR_INVALID_ARGS;
    memcpy(&config, cmd, sizeof(config));

    if (ipm->active)
        return ZX_ERR_BAD_STATE;

    ipm_per_trace_state_t* per_trace = ipm->per_trace_state;
    // TODO(dje): Validate config.
    per_trace->config = config.config;
    per_trace->configured = true;
    return ZX_OK;
}

static zx_status_t ipm_stage_simple_perf_config(cpu_trace_device_t* dev,
                                                const void* cmd, size_t cmdlen) {
    zxlogf(TRACE, "%s called\n", __func__);

    ipm_device_t* ipm = dev->ipm;
    if (!ipm)
        return ZX_ERR_BAD_STATE;

    ioctl_ipm_simple_perf_config_t simple_config;
    if (cmdlen != sizeof(simple_config))
        return ZX_ERR_INVALID_ARGS;
    memcpy(&simple_config, cmd, sizeof(simple_config));

    if (ipm->active)
        return ZX_ERR_BAD_STATE;

    ipm_per_trace_state_t* per_trace = ipm->per_trace_state;
    zx_x86_ipm_perf_config_t config;
    zx_status_t status = simple_config_to_cpu_config(&simple_config, &config);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: simple_config_to_cpu_config failed, %d\n",
               __func__, status);
        return status;
    }

    per_trace->config = config;
    per_trace->configured = true;
    return ZX_OK;
}

static zx_status_t ipm_get_perf_config(cpu_trace_device_t* dev,
                                       void* reply, size_t replymax,
                                       size_t* out_actual) {
    zxlogf(TRACE, "%s called\n", __func__);

    ipm_device_t* ipm = dev->ipm;
    if (!ipm)
        return ZX_ERR_BAD_STATE;

    ipm_per_trace_state_t* per_trace = ipm->per_trace_state;
    if (!per_trace->configured)
        return ZX_ERR_BAD_STATE;

    ioctl_ipm_perf_config_t config;
    if (replymax < sizeof(config))
        return ZX_ERR_BUFFER_TOO_SMALL;

    config.config = per_trace->config;
    memcpy(reply, &config, sizeof(config));
    *out_actual = sizeof(config);
    return ZX_OK;
}

static zx_status_t ipm_start(cpu_trace_device_t* dev) {
    zxlogf(TRACE, "%s called\n", __func__);

    ipm_device_t* ipm = dev->ipm;
    if (!ipm)
        return ZX_ERR_BAD_STATE;

    if (ipm->active)
        return ZX_ERR_BAD_STATE;

    ipm_per_trace_state_t* per_trace = ipm->per_trace_state;
    if (!per_trace->configured)
        return ZX_ERR_BAD_STATE;

    // Step 1: Get the configuration data into the kernel for use by START.
    // TODO(dje): Verify register values.
    // TODO(dje): Move to separate API calls?

    zxlogf(TRACE, "%s: fixed ctrl 0x%" PRIx64 ", global ctrl 0x%" PRIx64
           ", sample freq %u\n",
           __func__, per_trace->config.global_ctrl,
           per_trace->config.fixed_counter_ctrl,
           per_trace->config.sample_freq);

    // Require something to be enabled in order to start tracing.
    if (per_trace->config.global_ctrl == 0)
        return ZX_ERR_INVALID_ARGS;

    zx_handle_t resource = get_root_resource();

    zx_status_t status =
        zx_mtrace_control(resource, MTRACE_KIND_IPM,
                          MTRACE_IPM_INIT, 0, NULL, 0);
    if (status != ZX_OK)
        return status;

    uint32_t num_cpus = zx_system_get_num_cpus();
    for (uint32_t cpu = 0; cpu < num_cpus; ++cpu) {
        zx_x86_ipm_buffer_t buffer;
        io_buffer_t* io_buffer = &per_trace->buffers[cpu];
        buffer.vmo = io_buffer->vmo_handle;
        buffer.start_offset = 0;
        buffer.end_offset = io_buffer->size;
        status = zx_mtrace_control(resource, MTRACE_KIND_IPM,
                                   MTRACE_IPM_ASSIGN_BUFFER, cpu,
                                   &buffer, sizeof(buffer));
        if (status != ZX_OK)
            goto fail;
    }

    status = zx_mtrace_control(resource, MTRACE_KIND_IPM,
                               MTRACE_IPM_STAGE_CONFIG, 0,
                               &per_trace->config, sizeof(per_trace->config));
    if (status != ZX_OK)
        goto fail;

    // Step 2: Start data collection.

    status = zx_mtrace_control(resource, MTRACE_KIND_IPM, MTRACE_IPM_START,
                               0, NULL, 0);
    if (status != ZX_OK)
        goto fail;

    ipm->active = true;
    return ZX_OK;

  fail:
    {
        zx_status_t status2 =
            zx_mtrace_control(resource, MTRACE_KIND_IPM,
                              MTRACE_IPM_FINI, 0, NULL, 0);
        if (status2 != ZX_OK)
            zxlogf(TRACE, "%s: MTRACE_IPM_FINI failed: %d\n", __func__, status2);
        assert(status2 == ZX_OK);
        return status;
    }
}

static zx_status_t ipm_stop(cpu_trace_device_t* dev) {
    zxlogf(TRACE, "%s called\n", __func__);

    ipm_device_t* ipm = dev->ipm;
    if (!ipm)
        return ZX_ERR_BAD_STATE;

    zx_handle_t resource = get_root_resource();
    zx_status_t status =
        zx_mtrace_control(resource, MTRACE_KIND_IPM,
                          MTRACE_IPM_STOP, 0, NULL, 0);
    if (status == ZX_OK) {
        ipm->active = false;
        // TODO(dje): Move this to separate API call?
        status = zx_mtrace_control(resource, MTRACE_KIND_IPM,
                                   MTRACE_IPM_FINI, 0, NULL, 0);
    }
    return status;
}

zx_status_t ipm_ioctl(cpu_trace_device_t* dev, uint32_t op,
                      const void* cmd, size_t cmdlen,
                      void* reply, size_t replymax,
                      size_t* out_actual) {
    assert(IOCTL_FAMILY(op) == IOCTL_FAMILY_IPM);

    switch (op) {
    case IOCTL_IPM_GET_STATE:
        if (cmdlen != 0)
            return ZX_ERR_INVALID_ARGS;
        return ipm_get_state(dev, reply, replymax, out_actual);

    case IOCTL_IPM_ALLOC_TRACE:
        if (replymax != 0)
            return ZX_ERR_INVALID_ARGS;
        return ipm_alloc_trace(dev, cmd, cmdlen);

    case IOCTL_IPM_FREE_TRACE:
        if (cmdlen != 0 || replymax != 0)
            return ZX_ERR_INVALID_ARGS;
        return ipm_free_trace(dev);

    case IOCTL_IPM_GET_TRACE_CONFIG:
        if (cmdlen != 0)
            return ZX_ERR_INVALID_ARGS;
        return ipm_get_trace_config(dev, reply, replymax, out_actual);

    case IOCTL_IPM_GET_BUFFER_INFO:
        return ipm_get_buffer_info(dev, cmd, cmdlen, reply, replymax, out_actual);

    case IOCTL_IPM_GET_BUFFER_HANDLE:
        return ipm_get_buffer_handle(dev, cmd, cmdlen, reply, replymax, out_actual);

    case IOCTL_IPM_STAGE_PERF_CONFIG:
        if (replymax != 0)
            return ZX_ERR_INVALID_ARGS;
        return ipm_stage_perf_config(dev, cmd, cmdlen);

    case IOCTL_IPM_STAGE_SIMPLE_PERF_CONFIG:
        if (replymax != 0)
            return ZX_ERR_INVALID_ARGS;
        return ipm_stage_simple_perf_config(dev, cmd, cmdlen);

    case IOCTL_IPM_GET_PERF_CONFIG:
        return ipm_get_perf_config(dev, reply, replymax, out_actual);

    case IOCTL_IPM_START:
        if (cmdlen != 0 || replymax != 0)
            return ZX_ERR_INVALID_ARGS;
        return ipm_start(dev);

    case IOCTL_IPM_STOP:
        if (cmdlen != 0 || replymax != 0)
            return ZX_ERR_INVALID_ARGS;
        return ipm_stop(dev);

    default:
        return ZX_ERR_INVALID_ARGS;
    }
}

void ipm_release(cpu_trace_device_t* dev) {
    // TODO(dje): None of these should fail. What to do?
    // Suggest flagging things as busted and prevent further use.
    ipm_stop(dev);
    ipm_free_trace(dev);
}
