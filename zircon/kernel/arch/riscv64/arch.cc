// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include <arch.h>
#include <assert.h>
#include <bits.h>
#include <debug.h>
#include <inttypes.h>
#include <lib/arch/intrin.h>
#include <lib/cmdline.h>
#include <platform.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <arch/mp.h>
#include <arch/vm.h>
#include <arch/ops.h>
#include <arch/riscv64/sbi.h>
#include <arch/regs.h>
#include <kernel/atomic.h>
#include <kernel/thread.h>
#include <kernel/percpu.h>
#include <kernel/scheduler.h>
#include <lk/init.h>
#include <lk/main.h>

#define LOCAL_TRACE 0

// per cpu structure, pointed to by s11 (x27)
struct riscv64_percpu percpu[SMP_MAX_CPUS];

// Used to hold up the boot sequence on secondary CPUs until signaled by the primary.
static ktl::atomic<bool> secondaries_released;

// one for each secondary CPU, indexed by (cpu_num - 1).
static Thread _init_thread[SMP_MAX_CPUS - 1];

// first C level code to initialize each cpu
void riscv64_early_init_percpu(void) {
  // set the top level exception handler
  riscv64_csr_write(RISCV64_CSR_STVEC, (uintptr_t)&riscv64_exception_entry);

  // mask all exceptions, just in case
  riscv64_csr_clear(RISCV64_CSR_SSTATUS, RISCV64_CSR_SSTATUS_IE);
  riscv64_csr_clear(RISCV64_CSR_SIE, RISCV64_CSR_SIE_SIE | RISCV64_CSR_SIE_TIE | RISCV64_CSR_SIE_EIE);
}

void arch_early_init() {
  riscv64_early_init_percpu();
}

void arch_prevm_init() {
}

void arch_init() TA_NO_THREAD_SAFETY_ANALYSIS {
  // print some arch info
  dprintf(INFO, "RISCV: Supervisor mode\n");
  dprintf(INFO, "RISCV: mvendorid %#lx marchid %#lx mimpid %#lx\n",
          sbi_call(SBI_GET_MVENDORID).value, sbi_call(SBI_GET_MARCHID).value,
          sbi_call(SBI_GET_MIMPID).value);
  dprintf(INFO, "RISCV: MMU enabled sv49\n");
  dprintf(INFO, "RISCV: SBI impl id %#lx version %#lx\n", sbi_call(SBI_GET_SBI_IMPL_ID).value, sbi_call(SBI_GET_SBI_IMPL_VERSION).value);

  // probe some SBI extensions
  dprintf(INFO, "RISCV: SBI extension TIMER %ld\n", sbi_call(SBI_PROBE_EXTENSION, SBI_EXT_TIMER).value);
  dprintf(INFO, "RISCV: SBI extension IPI %ld\n", sbi_call(SBI_PROBE_EXTENSION, SBI_EXT_IPI).value);
  dprintf(INFO, "RISCV: SBI extension RFENCE %ld\n", sbi_call(SBI_PROBE_EXTENSION, SBI_EXT_RFENCE).value);
  dprintf(INFO, "RISCV: SBI extension HSM %ld\n", sbi_call(SBI_PROBE_EXTENSION, SBI_EXT_HSM).value);

  uint32_t max_cpus = arch_max_num_cpus();
  uint32_t cmdline_max_cpus = gCmdline.GetUInt32("kernel.smp.maxcpus", max_cpus);
  if (cmdline_max_cpus > max_cpus || cmdline_max_cpus <= 0) {
    printf("invalid kernel.smp.maxcpus value, defaulting to %u\n", max_cpus);
    cmdline_max_cpus = max_cpus;
  }

  int secondaries_to_init = cmdline_max_cpus - 1;
  lk_init_secondary_cpus(secondaries_to_init);
  LTRACEF("releasing %d secondary cpus\n", secondaries_to_init);
  secondaries_released.store(true);
}

void arch_late_init_percpu(void) {
  // enable software interrupts, used for inter-processor-interrupts
  riscv64_csr_set(RISCV64_CSR_SIE, RISCV64_CSR_SIE_SIE);

  // enable external interrupts
  riscv64_csr_set(RISCV64_CSR_SIE, RISCV64_CSR_SIE_EIE);

  mp_set_curr_cpu_online(true);
}

__NO_RETURN int arch_idle_thread_routine(void*) {
  for (;;) {
    __asm__ volatile("wfi");
  }
}

void arch_setup_uspace_iframe(iframe_t* iframe, uintptr_t pc, uintptr_t sp, uintptr_t arg1,
                              uintptr_t arg2) {
  iframe->epc = pc;
  iframe->sp = sp;
  iframe->status = RISCV64_CSR_SSTATUS_PIE | RISCV64_CSR_SSTATUS_IE;
  iframe->a0 = arg1;
  iframe->a1 = arg2;
}

extern "C" void riscv64_uspace_entry(iframe_t *iframe, vaddr_t tp);

// Switch to user mode, set the user stack pointer to user_stack_top, save the
// top of the kernel stack pointer.
void arch_enter_uspace(iframe_t* iframe) {
  Thread* ct = Thread::Current::Get();

  LTRACEF("riscv64_uspace_entry(%#" PRIxPTR ", %#" PRIxPTR ", %#" PRIxPTR ", %#" PRIxPTR ")\n",
          iframe->a0, iframe->a1, ct->stack().top(), iframe->epc);

  arch_disable_ints();

  ASSERT(arch_is_valid_user_pc(iframe->epc));

  riscv64_uspace_entry(iframe, ct->stack().top());
  __UNREACHABLE;
}

/* unimplemented cache operations */
void arch_disable_cache(uint flags) { }
void arch_enable_cache(uint flags) { }

void arch_clean_cache_range(vaddr_t start, size_t len) { }
void arch_clean_invalidate_cache_range(vaddr_t start, size_t len) { }
void arch_invalidate_cache_range(vaddr_t start, size_t len) { }
void arch_sync_cache_range(vaddr_t start, size_t len) { }

zx_status_t riscv64_create_secondary_stack(cpu_num_t cpu_num, vaddr_t *sp) {
  DEBUG_ASSERT_MSG(cpu_num > 0 && cpu_num < SMP_MAX_CPUS, "cpu_num: %u", cpu_num);
  KernelStack* stack = &_init_thread[cpu_num - 1].stack();
  DEBUG_ASSERT(stack->base() == 0);
  zx_status_t status = stack->Init();
  if (status != ZX_OK) {
    return status;
  }

  // Store cpu_num on the stack
  *(((uint64_t *)stack->top()) - 1) = cpu_num;

  // Store the stack pointer for our caller
  *sp = stack->top();

  return ZX_OK;
}

zx_status_t riscv64_free_secondary_stack(cpu_num_t cpu_num) {
  DEBUG_ASSERT(cpu_num > 0 && cpu_num < SMP_MAX_CPUS);
  return _init_thread[cpu_num - 1].stack().Teardown();
}

extern "C" void riscv64_secondary_entry(uint hart_id, uint cpu_num) {
  // basic bootstrapping of this cpu
  percpu[cpu_num].cpu_num = cpu_num;
  percpu[cpu_num].hart_id = hart_id;
  riscv64_set_percpu(&percpu[cpu_num]);
  arch_register_hart(cpu_num, hart_id);
  wmb();

  riscv64_early_init_percpu();

  // Wait until the primary has finished setting things up.
  while (!secondaries_released.load()) {
    arch::Yield();
  }

  _init_thread[cpu_num - 1].SecondaryCpuInitEarly();
  // Run early secondary cpu init routines up to the threading level.
  lk_init_level(LK_INIT_FLAG_SECONDARY_CPUS, LK_INIT_LEVEL_EARLIEST, LK_INIT_LEVEL_THREADING - 1);

  arch_mp_init_percpu();

  dprintf(INFO, "RISCV: secondary cpu %u coming up\n", cpu_num);

  lk_secondary_cpu_entry();
}

