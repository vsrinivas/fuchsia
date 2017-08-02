// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <err.h>
#include <stdint.h>

#include <magenta/compiler.h>
#include <magenta/device/intel-pt.h>

__BEGIN_CDECLS

void x86_processor_trace_init(void);

__END_CDECLS

#ifdef __cplusplus

typedef enum {
    IPT_TRACE_CPUS,
    IPT_TRACE_THREADS
} ipt_trace_mode_t;

status_t x86_ipt_set_mode(ipt_trace_mode_t mode);

status_t x86_ipt_cpu_mode_alloc();

status_t x86_ipt_cpu_mode_free();

status_t x86_ipt_cpu_mode_start();

status_t x86_ipt_cpu_mode_stop();

status_t x86_ipt_stage_cpu_data(uint32_t options, const mx_x86_pt_regs_t* regs);

status_t x86_ipt_get_cpu_data(uint32_t options, mx_x86_pt_regs_t* regs);

#endif // __cplusplus
