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
#include <arch/ops.h>
#include <arch/riscv64/sbi.h>
#include <arch/regs.h>
#include <kernel/atomic.h>
#include <kernel/thread.h>
#include <lk/init.h>
#include <lk/main.h>

// per cpu structure, pointed to by s11 (x27)
struct riscv64_percpu percpu[SMP_MAX_CPUS];

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
}

// Switch to user mode, set the user stack pointer to user_stack_top, put the svc stack pointer to
// the top of the kernel stack.
void arch_enter_uspace(iframe_t* iframe) {
  while (1) ;
}

/* unimplemented cache operations */
void arch_disable_cache(uint flags) { }
void arch_enable_cache(uint flags) { }

void arch_clean_cache_range(vaddr_t start, size_t len) { }
void arch_clean_invalidate_cache_range(vaddr_t start, size_t len) { }
void arch_invalidate_cache_range(vaddr_t start, size_t len) { }
void arch_sync_cache_range(vaddr_t start, size_t len) { }

