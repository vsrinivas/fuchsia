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
#include <arch/riscv64/sbi.h>

#define LOCAL_TRACE 0

// mapping of cpu -> hart
uint64_t cpu_to_hart_map[SMP_MAX_CPUS] = {0};

// list of IPIs queued per cpu
static volatile ktl::atomic<int> ipi_data[SMP_MAX_CPUS];

uint riscv64_num_cpus = 1;

void arch_register_hart(uint cpu_num, uint64_t hart_id) {
  cpu_to_hart_map[cpu_num] = hart_id;
}

// software triggered exceptions, used for cross-cpu calls
void riscv64_software_exception(void) {
  uint current_hart = riscv64_curr_hart_id();

  sbi_clear_ipi();

  rmb();
  int reason = ipi_data[current_hart].exchange(0);
  LTRACEF("current_hart %u reason %#x\n", current_hart, reason);

  if (reason & (1u << MP_IPI_RESCHEDULE)) {
    mp_mbx_reschedule_irq(nullptr);
    reason &= ~(1u << MP_IPI_RESCHEDULE);
  }
  if (reason & (1u << MP_IPI_GENERIC)) {
    panic("unimplemented MP_IPI_GENERIC\n");
    reason &= ~(1u << MP_IPI_GENERIC);
  }
  if (reason & (1u << MP_IPI_INTERRUPT)) {
    panic("unimplemented MP_IPI_INTERRUPT\n");
    reason &= ~(1u << MP_IPI_INTERRUPT);
  }
  if (reason & (1u << MP_IPI_HALT)) {
    panic("unimplemented MP_IPI_HALT\n");
    reason &= ~(1u << MP_IPI_HALT);
  }

  if (unlikely(reason)) {
    TRACEF("unhandled ipi cause %#x, hartid %#x\n", reason, current_hart);
    panic("stopping");
  }
}

void arch_prepare_current_cpu_idle_state(bool idle) {
  // no-op
}

void arch_mp_reschedule(cpu_mask_t mask) {
  arch_mp_send_ipi(MP_IPI_TARGET_MASK, mask, MP_IPI_RESCHEDULE);
}

void arch_mp_send_ipi(mp_ipi_target_t target, cpu_mask_t mask, mp_ipi_t ipi) {
  LTRACEF("target %d mask %#x, ipi %d\n", target, mask, ipi);

  ulong hart_mask = 0;

  // translate the high level target + mask mechanism into just a mask
  switch (target) {
    case MP_IPI_TARGET_ALL:
      hart_mask = (1ul << SMP_MAX_CPUS) - 1;
      break;
    case MP_IPI_TARGET_ALL_BUT_LOCAL:
      hart_mask = mask_all_but_one(riscv64_curr_hart_id());
      break;
    case MP_IPI_TARGET_MASK:
      for (uint cpu = 0; cpu < SMP_MAX_CPUS && mask; cpu++, mask >>= 1) {
        if (mask & 1) {
          uint64_t hart = cpu_to_hart_map[cpu];
          LTRACEF("cpu %u hart %lu mask %#x\n", cpu, hart, mask);

          // record a pending hart to notify
          hart_mask |= (1ul << hart);

          // set the ipi_data based on the incoming ipi
          ipi_data[hart].fetch_or(1u << ipi);
        }
      }
      break;
  }

  mb();
  LTRACEF("sending to hart_mask %#lx\n", hart_mask);
  sbi_send_ipis(&hart_mask);
}

void arch_mp_init_percpu(void) {
  interrupt_init_percpu();
}

void arch_flush_state_and_halt(Event* flush_done) {
  DEBUG_ASSERT(arch_ints_disabled());
  flush_done->SignalNoResched();
  platform_halt_cpu();
  panic("control should never reach here\n");
}

zx_status_t arch_mp_prep_cpu_unplug(uint cpu_id) {
  if (cpu_id == 0 || cpu_id >= riscv64_num_cpus) {
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

zx_status_t arch_mp_cpu_unplug(uint cpu_id) {
  // we do not allow unplugging the bootstrap processor
  if (cpu_id == 0 || cpu_id >= riscv64_num_cpus) {
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

zx_status_t arch_mp_cpu_hotplug(cpu_num_t cpu_id) { return ZX_ERR_NOT_SUPPORTED; }

