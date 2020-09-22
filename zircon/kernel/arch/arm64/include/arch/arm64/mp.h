// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_MP_H_
#define ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_MP_H_

#include <zircon/compiler.h>

#include <arch/arm64.h>
#include <arch/arm64/feature.h>
#include <kernel/align.h>
#include <kernel/cpu.h>

__BEGIN_CDECLS

// bits for mpidr register
#define MPIDR_AFF0_MASK 0xFFULL
#define MPIDR_AFF0_SHIFT 0
#define MPIDR_AFF1_MASK (0xFFULL << 8)
#define MPIDR_AFF1_SHIFT 8
#define MPIDR_AFF2_MASK (0xFFULL << 16)
#define MPIDR_AFF2_SHIFT 16
#define MPIDR_AFF3_MASK (0xFFULL << 32)
#define MPIDR_AFF3_SHIFT 32

// construct a ARM MPID from cluster (AFF1) and cpu number (AFF0)
#define ARM64_MPID(cluster, cpu)                       \
  (((cluster << MPIDR_AFF1_SHIFT) & MPIDR_AFF1_MASK) | \
   ((cpu << MPIDR_AFF0_SHIFT) & MPIDR_AFF0_MASK))

// TODO: add support for AFF2 and AFF3

// Per cpu structure, pointed to by a fixed register while in kernel mode.
// Aligned on the maximum architectural cache line to avoid cache
// line sharing between cpus.
struct arm64_percpu {
  // cpu number
  cpu_num_t cpu_num;

  // Whether blocking is disallowed.  See arch_blocking_disallowed().
  uint32_t blocking_disallowed;

  // Number of spinlocks currently held.
  uint32_t num_spinlocks;

  // Microarchitecture of this cpu (ex: Cortex-A53)
  arm64_microarch microarch;

  // True if the branch predictor should be invalidated during context switch or on suspicious
  // entries to EL2 to mitigate Spectre V2 attacks.
  bool should_invalidate_bp_on_context_switch;
} __CPU_ALIGN;

void arch_init_cpu_map(uint cluster_count, const uint* cluster_cpus);
void arch_register_mpid(uint cpu_id, uint64_t mpid);
void arm64_init_percpu_early(void);

// Use the x15 register to always point at the local cpu structure to allow fast access
// a per cpu structure.
// Do not directly access fields of this structure
register struct arm64_percpu* __arm64_percpu __asm("x15");

static inline void arm64_write_percpu_ptr(struct arm64_percpu* percpu) { __arm64_percpu = percpu; }

static inline struct arm64_percpu* arm64_read_percpu_ptr(void) { return __arm64_percpu; }

static inline uint32_t arm64_read_percpu_u32(size_t offset) {
  uint32_t val;

  // mark as volatile to force a read of the field to make sure
  // the compiler always emits a read when asked and does not cache
  // a copy between
  __asm__ volatile("ldr %w[val], [x15, %[offset]]" : [val] "=r"(val) : [offset] "Ir"(offset));
  return val;
}

static inline void arm64_write_percpu_u32(size_t offset, uint32_t val) {
  __asm__("str %w[val], [x15, %[offset]]" ::[val] "r"(val), [offset] "Ir"(offset) : "memory");
}

static inline cpu_num_t arch_curr_cpu_num(void) {
  return arm64_read_percpu_u32(offsetof(struct arm64_percpu, cpu_num));
}

// TODO(fxbug.dev/32903) get num_cpus from topology.
// This needs to be set very early (before arch_init).
static inline void arch_set_num_cpus(uint cpu_count) {
  extern uint arm_num_cpus;
  arm_num_cpus = cpu_count;
}

static inline uint arch_max_num_cpus(void) {
  extern uint arm_num_cpus;

  return arm_num_cpus;
}

// translate a cpu number back to the cluster ID (AFF1)
static inline uint arch_cpu_num_to_cluster_id(cpu_num_t cpu) {
  extern uint arm64_cpu_cluster_ids[SMP_MAX_CPUS];

  return arm64_cpu_cluster_ids[cpu];
}

// translate a cpu number back to the MP cpu number within a cluster (AFF0)
static inline uint arch_cpu_num_to_cpu_id(cpu_num_t cpu) {
  extern uint arm64_cpu_cpu_ids[SMP_MAX_CPUS];

  return arm64_cpu_cpu_ids[cpu];
}

// translate mpidr to cpu number
cpu_num_t arm64_mpidr_to_cpu_num(uint64_t mpidr);

#define READ_PERCPU_FIELD32(field) arm64_read_percpu_u32(offsetof(struct arm64_percpu, field))

#define WRITE_PERCPU_FIELD32(field, value) \
  arm64_write_percpu_u32(offsetof(struct arm64_percpu, field), (value))

__END_CDECLS

#endif  // ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_MP_H_
