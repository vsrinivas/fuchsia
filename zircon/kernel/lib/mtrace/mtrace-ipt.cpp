// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifdef __x86_64__ // entire file

#include <assert.h>
#include <inttypes.h>

#include "lib/mtrace.h"
#include "trace.h"

#include <object/process_dispatcher.h>
#include <lib/zircon-internal/mtrace.h>
#include <lib/zircon-internal/device/cpu-trace/intel-pt.h>

#include "arch/x86/proc_trace.h"

#define LOCAL_TRACE 0

static_assert(IPT_MAX_NUM_TRACES >= SMP_MAX_CPUS, "");

zx_status_t mtrace_insntrace_control(uint32_t action, uint32_t options,
                                     user_inout_ptr<void> arg, size_t size) {
    TRACEF("action %u, options 0x%x, arg %p, size 0x%zx\n",
           action, options, arg.get(), size);

    switch (action) {
    case MTRACE_INSNTRACE_ALLOC_TRACE: {
        if (options != 0)
            return ZX_ERR_INVALID_ARGS;
        ioctl_insntrace_trace_config_t config;
        if (size != sizeof(config))
            return ZX_ERR_INVALID_ARGS;
        zx_status_t status = arg.reinterpret<ioctl_insntrace_trace_config_t>().copy_from_user(&config);
        if (status != ZX_OK)
            return status;
        TRACEF("action %u, mode %u, num traces %u\n", action, config.mode, config.num_traces);
        if (config.num_traces > IPT_MAX_NUM_TRACES)
            return ZX_ERR_INVALID_ARGS;
        switch (config.mode) {
        case IPT_MODE_CPUS:
            if (config.num_traces != arch_max_num_cpus())
                return ZX_ERR_INVALID_ARGS;
            return x86_ipt_alloc_trace(IPT_TRACE_CPUS, config.num_traces);
        case IPT_MODE_THREADS:
            return x86_ipt_alloc_trace(IPT_TRACE_THREADS, config.num_traces);
        default:
            return ZX_ERR_INVALID_ARGS;
        }
    }

    case MTRACE_INSNTRACE_FREE_TRACE:
        if (options != 0 || size != 0)
            return ZX_ERR_INVALID_ARGS;
        return x86_ipt_free_trace();

    case MTRACE_INSNTRACE_STAGE_TRACE_DATA: {
        zx_x86_pt_regs_t regs;
        if (size != sizeof(regs))
            return ZX_ERR_INVALID_ARGS;
        zx_status_t status = arg.reinterpret<zx_x86_pt_regs_t>().copy_from_user(&regs);
        if (status != ZX_OK)
            return status;
        zx_itrace_buffer_descriptor_t descriptor = options;
        TRACEF("action %u, descriptor %u, ctl 0x%" PRIx64 ", output_base 0x%" PRIx64 "\n",
               action, descriptor, regs.ctl, regs.output_base);
        return x86_ipt_stage_trace_data(descriptor, &regs);
    }

    case MTRACE_INSNTRACE_GET_TRACE_DATA: {
        zx_x86_pt_regs_t regs;
        if (size != sizeof(regs))
            return ZX_ERR_INVALID_ARGS;
        zx_itrace_buffer_descriptor_t descriptor = options;
        auto status = x86_ipt_get_trace_data(descriptor, &regs);
        if (status != ZX_OK)
            return status;
        TRACEF("action %u, descriptor %u, ctl 0x%" PRIx64 ", output_base 0x%" PRIx64 "\n",
               action, descriptor, regs.ctl, regs.output_base);
        status = arg.reinterpret<zx_x86_pt_regs_t>().copy_to_user(regs);
        if (status != ZX_OK)
            return status;
        return ZX_OK;
    }

    case MTRACE_INSNTRACE_START:
        if (options != 0 || size != 0)
            return ZX_ERR_INVALID_ARGS;
        return x86_ipt_start();

    case MTRACE_INSNTRACE_STOP:
        if (options != 0 || size != 0)
            return ZX_ERR_INVALID_ARGS;
        return x86_ipt_stop();

    default:
        return ZX_ERR_INVALID_ARGS;
    }
}

#endif
