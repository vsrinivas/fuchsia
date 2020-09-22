// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include <assert.h>
#include <err.h>
#include <platform.h>
#include <trace.h>
#include <zircon/types.h>

#include <arch/mp.h>
#include <arch/ops.h>
#include <dev/interrupt.h>
#include <kernel/cpu.h>
#include <kernel/event.h>

#define LOCAL_TRACE 0

namespace {
// Mask the MPIDR register to only leave the AFFx ids.
constexpr uint64_t kMpidAffMask = 0xFF00FFFFFF;

struct MpidCpuidPair {
  uint64_t mpid;
  uint cpu_id;
};

MpidCpuidPair arm64_cpu_list[SMP_MAX_CPUS];
size_t arm64_cpu_list_count = 0;

}  // namespace

// cpu id to cluster and id within cluster map
uint arm64_cpu_cluster_ids[SMP_MAX_CPUS] = {0};
uint arm64_cpu_cpu_ids[SMP_MAX_CPUS] = {0};

// total number of detected cpus
uint arm_num_cpus = 1;

// per cpu structures, each cpu will point to theirs using the fixed register
arm64_percpu arm64_percpu_array[SMP_MAX_CPUS];

void arch_register_mpid(uint cpu_id, uint64_t mpid) {
  // TODO(fxbug.dev/32903) transition off of these maps to the topology.
  arm64_cpu_cluster_ids[cpu_id] = (mpid & 0xFF00) >> MPIDR_AFF1_SHIFT;  // "cluster" here is AFF1.
  arm64_cpu_cpu_ids[cpu_id] = mpid & 0xFF;                              // "cpu" here is AFF0.

  arm64_percpu_array[cpu_id].cpu_num = cpu_id;

  arm64_cpu_list[arm64_cpu_list_count++] = {.mpid = mpid, .cpu_id = cpu_id};
}

cpu_num_t arm64_mpidr_to_cpu_num(uint64_t mpidr) {
  mpidr &= kMpidAffMask;
  for (size_t i = 0; i < arm64_cpu_list_count; ++i) {
    if (arm64_cpu_list[i].mpid == mpidr) {
      return arm64_cpu_list[i].cpu_id;
    }
  }

  if (arm64_cpu_list_count == 0) {
    // The only time we shouldn't find a cpu is when the list isn't
    // defined yet during early boot, in this case the only processor up is 0
    // so returning 0 is correct.
    return 0;
  }
  return INVALID_CPU;
}

// do the 'slow' lookup by mpidr to cpu number
static cpu_num_t arch_curr_cpu_num_slow() {
  uint64_t mpidr = __arm_rsr64("mpidr_el1");
  return arm64_mpidr_to_cpu_num(mpidr);
}

void arch_prepare_current_cpu_idle_state(bool idle) {
  // no-op
}

void arch_mp_reschedule(cpu_mask_t mask) {
  arch_mp_send_ipi(MP_IPI_TARGET_MASK, mask, MP_IPI_RESCHEDULE);
}

void arch_mp_send_ipi(mp_ipi_target_t target, cpu_mask_t mask, mp_ipi_t ipi) {
  LTRACEF("target %d mask %#x, ipi %d\n", target, mask, ipi);

  // translate the high level target + mask mechanism into just a mask
  switch (target) {
    case MP_IPI_TARGET_ALL:
      mask = (1ul << SMP_MAX_CPUS) - 1;
      break;
    case MP_IPI_TARGET_ALL_BUT_LOCAL:
      mask = mask_all_but_one(arch_curr_cpu_num());
      break;
    case MP_IPI_TARGET_MASK:;
  }

  interrupt_send_ipi(mask, ipi);
}

void arm64_init_percpu_early(void) {
  // slow lookup the current cpu id and setup the percpu structure
  cpu_num_t cpu = arch_curr_cpu_num_slow();
  uint32_t midr = __arm_rsr64("midr_el1") & 0xFFFFFFFF;

  arm64_percpu_array[cpu].microarch = midr_to_microarch(midr);
  arm64_write_percpu_ptr(&arm64_percpu_array[cpu]);
}

void arch_mp_init_percpu(void) { interrupt_init_percpu(); }

void arch_flush_state_and_halt(Event* flush_done) {
  DEBUG_ASSERT(arch_ints_disabled());
  flush_done->SignalNoResched();
  platform_halt_cpu();
  panic("control should never reach here\n");
}

zx_status_t arch_mp_prep_cpu_unplug(cpu_num_t cpu_id) {
  if (cpu_id == 0 || cpu_id >= arm_num_cpus) {
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

zx_status_t arch_mp_cpu_unplug(cpu_num_t cpu_id) {
  // we do not allow unplugging the bootstrap processor
  if (cpu_id == 0 || cpu_id >= arm_num_cpus) {
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

zx_status_t arch_mp_cpu_hotplug(cpu_num_t cpu_id) { return ZX_ERR_NOT_SUPPORTED; }
