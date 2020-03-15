// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_STATS_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_STATS_H_

#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// per cpu guest statistics
struct guest_stats {
  ulong vm_entries;
  ulong vm_exits;
#ifdef __aarch64__
  ulong wfi_wfe_instructions;
  ulong instruction_aborts;
  ulong data_aborts;
  ulong system_instructions;
  ulong smc_instructions;
  ulong interrupts;
#else
  ulong interrupts;
  ulong interrupt_windows;
  ulong cpuid_instructions;
  ulong hlt_instructions;
  ulong control_register_accesses;
  ulong io_instructions;
  ulong rdmsr_instructions;
  ulong wrmsr_instructions;
  ulong ept_violations;
  ulong xsetbv_instructions;
  ulong pause_instructions;
  ulong vmcall_instructions;
#endif
};

// per cpu kernel level statistics
struct cpu_stats {
  zx_duration_t idle_time;
  ulong reschedules;
  ulong context_switches;
  ulong irq_preempts;
  ulong preempts;
  ulong yields;

  // cpu level interrupts and exceptions
  ulong interrupts;  // hardware interrupts, minus timer interrupts or inter-processor interrupts
  ulong timer_ints;  // timer interrupts
  ulong timers;      // timer callbacks
  ulong perf_ints;   // performance monitor interrupts
  ulong syscalls;
  ulong page_faults;

  // inter-processor interrupts
  ulong reschedule_ipis;
  ulong generic_ipis;
};

__END_CDECLS

// include after the cpu_stats definition above, since it is part of the percpu structure
#include <kernel/percpu.h>

#define GUEST_STATS_INC(name)                                                   \
  do {                                                                          \
    __atomic_fetch_add(&get_local_percpu()->gstats.name, 1u, __ATOMIC_RELAXED); \
  } while (0)

#define CPU_STATS_INC(name)                                                    \
  do {                                                                         \
    __atomic_fetch_add(&get_local_percpu()->stats.name, 1u, __ATOMIC_RELAXED); \
  } while (0)

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_STATS_H_
