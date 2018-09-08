// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <err.h>
#include <stdint.h>

#include <zircon/compiler.h>

#include <fbl/ref_ptr.h>
#include <vm/vm_object.h>

#include <arch/x86.h>

#include <zircon/device/cpu-trace/intel-pm.h>

__BEGIN_CDECLS

void x86_perfmon_init(void);

__END_CDECLS

#ifdef __cplusplus

zx_status_t x86_ipm_get_properties(zx_x86_ipm_properties_t* state);

zx_status_t x86_ipm_init();

zx_status_t x86_ipm_assign_buffer(uint32_t cpu, fbl::RefPtr<VmObject> vmo);

zx_status_t x86_ipm_stage_config(zx_x86_ipm_config_t* config);

zx_status_t x86_ipm_start();

zx_status_t x86_ipm_stop();

zx_status_t x86_ipm_fini();

void apic_pmi_interrupt_handler(x86_iframe_t *frame);

#endif // __cplusplus
