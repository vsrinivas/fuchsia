// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include <assert.h>
#include <platform.h>
#include <trace.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <arch/mp.h>
#include <arch/ops.h>
#include <dev/interrupt.h>
#include <kernel/event.h>

void arch_prepare_current_cpu_idle_state(bool idle) {
}

void arch_mp_reschedule(cpu_mask_t mask) {
}

void arch_mp_send_ipi(mp_ipi_target_t target, cpu_mask_t mask, mp_ipi_t ipi) {
}

void arch_mp_init_percpu(void) {
}

void arch_flush_state_and_halt(Event* flush_done) {
  while(1) ;
}

zx_status_t arch_mp_prep_cpu_unplug(uint cpu_id) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t arch_mp_cpu_unplug(uint cpu_id) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t arch_mp_cpu_hotplug(cpu_num_t cpu_id) { return ZX_ERR_NOT_SUPPORTED; }

