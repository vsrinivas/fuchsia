// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifdef __x86_64__ // entire file

#include <inttypes.h>

#include "lib/mtrace.h"
#include "trace.h"

#include <lib/zircon-internal/mtrace.h>

#include "arch/x86/proc_trace.h"

#define LOCAL_TRACE 0

zx_status_t mtrace_insntrace_control(uint32_t action, uint32_t options,
                                     user_inout_ptr<void> arg, size_t size) {
    TRACEF("action %u, options 0x%x, arg %p, size 0x%zx\n",
           action, options, arg.get(), size);

    switch (action) {
    case MTRACE_INSNTRACE_ALLOC_TRACE: {
        if (options != 0)
            return ZX_ERR_INVALID_ARGS;
        uint32_t mode;
        if (size != sizeof(mode))
            return ZX_ERR_INVALID_ARGS;
        zx_status_t status = arg.reinterpret<uint32_t>().copy_from_user(&mode);
        if (status != ZX_OK)
            return status;
        TRACEF("action %u, mode 0x%x\n", action, mode);
        switch (mode) {
        case IPT_MODE_CPUS:
            return x86_ipt_alloc_trace(IPT_TRACE_CPUS);
        case IPT_MODE_THREADS:
            return x86_ipt_alloc_trace(IPT_TRACE_THREADS);
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
