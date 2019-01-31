// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <err.h>
#include <stdint.h>

#include <lib/zircon-internal/device/cpu-trace/intel-pt.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

void x86_processor_trace_init(void);

__END_CDECLS

#ifdef __cplusplus

typedef enum {
    IPT_TRACE_CPUS,
    IPT_TRACE_THREADS
} ipt_trace_mode_t;

zx_status_t x86_ipt_alloc_trace(ipt_trace_mode_t mode, uint32_t num_traces);

zx_status_t x86_ipt_free_trace();

zx_status_t x86_ipt_start();

zx_status_t x86_ipt_stop();

zx_status_t x86_ipt_stage_trace_data(zx_itrace_buffer_descriptor_t descriptor,
                                     const zx_x86_pt_regs_t* regs);

zx_status_t x86_ipt_get_trace_data(zx_itrace_buffer_descriptor_t descriptor,
                                   zx_x86_pt_regs_t* regs);

#endif // __cplusplus
