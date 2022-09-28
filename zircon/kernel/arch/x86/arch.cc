// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2015 Intel Corporation
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <align.h>
#include <arch.h>
#include <assert.h>
#include <debug.h>
#include <inttypes.h>
#include <lib/arch/x86/boot-cpuid.h>
#include <lib/arch/x86/lbr.h>
#include <lib/backtrace/global_cpu_context_exchange.h>
#include <lib/console.h>
#include <lib/version.h>
#include <platform.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <trace.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <arch/mp.h>
#include <arch/ops.h>
#include <arch/regs.h>
#include <arch/vm.h>
#include <arch/x86/apic.h>
#include <arch/x86/descriptor.h>
#include <arch/x86/feature.h>
#include <arch/x86/interrupts.h>
#include <arch/x86/mmu.h>
#include <arch/x86/mmu_mem_types.h>
#include <arch/x86/mp.h>
#include <hwreg/x86msr.h>
#include <kernel/cpu.h>
#include <kernel/mp.h>
#include <kernel/percpu.h>
#include <lk/init.h>
#include <lk/main.h>
#include <vm/vm.h>

#include "arch/x86.h"

#define LOCAL_TRACE 0

#ifndef __GCC_HAVE_SYNC_COMPARE_AND_SWAP_16
#error "missing -mcx16"
#endif

namespace {
void EnableLbrs(cpu_mask_t mask) {
  mp_sync_exec(
      MP_IPI_TARGET_MASK, mask,
      [](void*) {
        arch::LbrStack stack(arch::BootCpuidIo{});
        if (stack.is_supported()) {
          stack.Enable(hwreg::X86MsrIo{}, false);
          printf("CPU-%u: LBRs enabled\n", arch_curr_cpu_num());
        } else {
          printf("CPU-%u: LBRs are not supported\n", arch_curr_cpu_num());
        }
      },
      nullptr);
}

void DisableLbrs(cpu_mask_t mask) {
  mp_sync_exec(
      MP_IPI_TARGET_MASK, mask,
      [](void*) {
        arch::LbrStack stack(arch::BootCpuidIo{});
        if (stack.is_supported()) {
          stack.Disable(hwreg::X86MsrIo{});
          printf("CPU-%u: LBRs disabled\n", arch_curr_cpu_num());
        } else {
          printf("CPU-%u: LBRs are not supported\n", arch_curr_cpu_num());
        }
      },
      nullptr);
}

void DumpLbrs(cpu_num_t cpu_num) {
  mp_sync_exec(
      MP_IPI_TARGET_MASK, cpu_num_to_mask(cpu_num),
      [](void* ctx) {
        arch::LbrStack stack(arch::BootCpuidIo{});
        hwreg::X86MsrIo io;
        uint32_t cpu_num = *reinterpret_cast<uint32_t*>(ctx);
        if (stack.is_enabled(io)) {
          PrintSymbolizerContext(stdout);
          printf("CPU-%u: Last Branch Records (omitting records braching to userspace)\n", cpu_num);
          stack.ForEachRecord(io, [](const arch::LastBranchRecord& lbr) {
            // Only include branches that end in the kernel, as we cannot make
            // sense of any recorded userspace code; we do not know a priori at
            // which addresses the relevant modules were loaded.
            if (is_kernel_address(lbr.to)) {
              printf("from: {{{pc:%#" PRIxPTR "}}}\n", lbr.from);
              printf("to: {{{pc:%#" PRIxPTR "}}}\n", lbr.to);
            }
          });
        } else {
          printf("CPU-%u: LBRs are not enabled\n", cpu_num);
        }
      },
      &cpu_num);
}

int LbrCtrl(int argc, const cmd_args* argv, uint32_t flags) {
  auto print_usage = [&]() {
    printf("usage:\n");
    printf("%s lbr enable [cpu mask = CPU_MASK_ALL]\n", argv[0].str);
    printf("%s lbr disable [cpu mask = CPU_MASK_ALL]\n", argv[0].str);
    printf("%s lbr dump [cpu num = 0]\n", argv[0].str);
  };

  if (argc < 3) {
    printf("not enough arguments\n");
    print_usage();
    return 1;
  }

  if (!strcmp(argv[2].str, "enable")) {
    cpu_mask_t mask = (argc > 3) ? static_cast<cpu_mask_t>(argv[3].u) : CPU_MASK_ALL;
    EnableLbrs(mask);
  } else if (!strcmp(argv[2].str, "disable")) {
    cpu_mask_t mask = (argc > 3) ? static_cast<cpu_mask_t>(argv[3].u) : CPU_MASK_ALL;
    DisableLbrs(mask);
  } else if (!strcmp(argv[2].str, "dump")) {
    auto cpu_num = (argc > 3) ? static_cast<cpu_num_t>(argv[3].u) : 0;
    DumpLbrs(cpu_num);
  } else {
    printf("unrecognized subcommand: %s\n", argv[2].str);
    print_usage();
    return 1;
  }

  return 0;
}

int GetContext(int argc, const cmd_args* argv, uint32_t flags) {
  auto print_usage = [&]() {
    printf("usage:\n");
    printf("%s context <cpu_id> <timeout_ms>\n", argv[0].str);
  };

  if (argc < 4) {
    printf("not enough arguments\n");
    print_usage();
    return 1;
  }

  cpu_num_t target = static_cast<cpu_num_t>(argv[2].u);
  if (target >= percpu::processor_count()) {
    printf("invalid cpu_id: %u\n", target);
    return 1;
  }

  const zx_duration_t timeout = ZX_MSEC(argv[2].u);

  printf("requesting context of CPU-%u\n", target);
  CpuContext context;
  zx_status_t status;
  {
    InterruptDisableGuard irqd;
    status = g_cpu_context_exchange.RequestContext(target, timeout, context);
  }
  if (status != ZX_OK) {
    printf("error: %d\n", status);
    return 1;
  }
  context.backtrace.Print();
  PrintFrame(stdout, context.frame);

  return 0;
}

}  // namespace

void arch_early_init(void) {
  x86_mmu_early_init();

  // Mark the boot cpu as online here after global constructors have run
  mp_set_curr_cpu_online(true);
}

void arch_prevm_init(void) { x86_cpu_feature_init(); }

void arch_init(void) {
  const struct x86_model_info* model = x86_get_model();
  printf("Processor Model Info: type %#x family %#x model %#x stepping %#x\n",
         model->processor_type, model->family, model->model, model->stepping);
  printf("\tdisplay_family %#x display_model %#x\n", model->display_family, model->display_model);

  x86_feature_debug();

  x86_mmu_init();

  gdt_setup();
  idt_setup_readonly();
}

void arch_late_init_percpu(void) {
  // Call per-CPU init function for the boot CPU.
  x86_cpu_feature_late_init_percpu();
}

void arch_setup_uspace_iframe(iframe_t* iframe, uintptr_t pc, uintptr_t sp, uintptr_t arg1,
                              uintptr_t arg2) {
  /* default user space flags:
   * IOPL 0
   * Interrupts enabled
   */
  iframe->flags = (0 << X86_FLAGS_IOPL_SHIFT) | X86_FLAGS_IF;

  iframe->cs = USER_CODE_64_SELECTOR;
  iframe->ip = pc;
  iframe->user_ss = USER_DATA_SELECTOR;
  iframe->user_sp = sp;

  iframe->rdi = arg1;
  iframe->rsi = arg2;
}

void arch_enter_uspace(iframe_t* iframe) {
  LTRACEF("entry %#" PRIxPTR " user stack %#" PRIxPTR "\n", iframe->ip, iframe->user_sp);
  LTRACEF("kernel stack %#" PRIxPTR "\n", x86_get_percpu()->default_tss.rsp0);
#if __has_feature(safe_stack)
  LTRACEF("kernel unsafe stack %#" PRIxPTR "\n", Thread::Current::Get()->stack().unsafe_top());
#endif

  arch_disable_ints();

  /* check that we are accessing userspace code */
  ASSERT(arch_is_valid_user_pc(iframe->ip));

  /* check that we're still pointed at the kernel gs */
  DEBUG_ASSERT(is_kernel_address(read_msr(X86_MSR_IA32_GS_BASE)));

  /* check that the kernel stack is set properly */
  DEBUG_ASSERT(is_kernel_address(x86_get_percpu()->default_tss.rsp0));

#if __has_feature(safe_stack)
  /* set the kernel unsafe stack back to the top as we enter user space */
  auto unsafe_top = Thread::Current::Get()->stack().unsafe_top();
  x86_uspace_entry(iframe, unsafe_top);
#else
  x86_uspace_entry(iframe);
#endif
  __UNREACHABLE;
}

void arch_prep_suspend(void) {
  DEBUG_ASSERT(arch_ints_disabled());
  apic_io_save();
}

void arch_resume(void) {
  DEBUG_ASSERT(arch_ints_disabled());

  x86_init_percpu(0);
  x86_mmu_percpu_init();
  mp_set_curr_cpu_online(true);
  x86_pat_sync(cpu_num_to_mask(0));

  apic_local_init();

  // Ensure the CPU that resumed was assigned the correct percpu object.
  DEBUG_ASSERT(apic_local_id() == x86_get_percpu()->apic_id);

  apic_io_restore();
}

[[noreturn, gnu::noinline]] static void finish_secondary_entry(
    ktl::atomic<unsigned int>* aps_still_booting, Thread* thread, uint cpu_num) {
  // Mark this cpu as online so MP code can try to deliver IPIs.
  // Mark here so any code waiting for the cpu to be started will see the cpu
  // online after the atomic below.
  mp_set_curr_cpu_online(true);

  // Signal that this CPU is initialized.  It is important that after this
  // operation, we do not touch any resources associated with bootstrap
  // besides our Thread and stack, since this is the checkpoint the
  // bootstrap process uses to identify completion.
  unsigned int old_val = aps_still_booting->fetch_and(~(1U << cpu_num));
  if (old_val == 0) {
    // If the value is already zero, then booting this CPU timed out.
    goto fail;
  }

  // Defer configuring memory settings until after the atomic_and above.
  // This ensures that we were in no-fill cache mode for the duration of early
  // AP init.
  DEBUG_ASSERT(x86_get_cr0() & X86_CR0_CD);
  x86_mmu_percpu_init();

  // Load the appropriate PAT/MTRRs.  This must happen after init_percpu, so
  // that this CPU is considered online.
  x86_pat_sync(1U << cpu_num);

  /* run early secondary cpu init routines up to the threading level */
  lk_init_level(LK_INIT_FLAG_SECONDARY_CPUS, LK_INIT_LEVEL_EARLIEST, LK_INIT_LEVEL_THREADING - 1);

  thread->SecondaryCpuInitEarly();
  // The thread stacks and struct are from a single allocation, free it
  // when we exit into the scheduler.
  thread->set_free_struct(true);

  lk_secondary_cpu_entry();

// lk_secondary_cpu_entry only returns on an error, halt the core in this
// case.
fail:
  arch_disable_ints();
  while (1) {
    x86_hlt();
  }
}

// This is called from assembly, before any other C code.
// The %gs.base is not set up yet, so we have to trust that
// this function is simple enough that the compiler won't
// want to generate stack-protector prologue/epilogue code,
// which would use %gs.
__NO_SAFESTACK __NO_RETURN void x86_secondary_entry(ktl::atomic<unsigned int>* aps_still_booting,
                                                    Thread* thread) {
  // Would prefer this to be in init_percpu, but there is a dependency on a
  // page mapping existing, and the BP calls that before the VM subsystem is
  // initialized.
  apic_local_init();

  uint32_t local_apic_id = apic_local_id();
  int cpu_num = x86_apic_id_to_cpu_num(local_apic_id);
  if (cpu_num < 0) {
    // If we could not find our CPU number, do not proceed further
    arch_disable_ints();
    while (1) {
      x86_hlt();
    }
  }

  DEBUG_ASSERT(cpu_num > 0);

  // Set %gs.base to our percpu struct.  This has to be done before
  // calling x86_init_percpu, which initializes most of that struct, so
  // that x86_init_percpu can use safe-stack and/or stack-protector code.
  struct x86_percpu* const percpu = &ap_percpus[cpu_num - 1];
  write_msr(X86_MSR_IA32_GS_BASE, (uintptr_t)percpu);

  // Copy the stack-guard value from the boot CPU's perpcu.
  percpu->stack_guard = bp_percpu.stack_guard;

#if __has_feature(safe_stack)
  // Set up the initial unsafe stack pointer.
  DEBUG_ASSERT(IS_ALIGNED(thread->stack().unsafe_top(), 16));
  x86_write_gs_offset64(ZX_TLS_UNSAFE_SP_OFFSET, thread->stack().unsafe_top());
#endif

  x86_init_percpu((uint)cpu_num);

  // Now do the rest of the work, in a function that is free to
  // use %gs in its code.
  finish_secondary_entry(aps_still_booting, thread, cpu_num);
}

static int cmd_cpu(int argc, const cmd_args* argv, uint32_t flags) {
  if (argc < 2) {
    printf("not enough arguments\n");
  usage:
    printf("usage:\n");
    printf("%s features\n", argv[0].str);
    printf("%s rdmsr <cpu_id> <msr_id>\n", argv[0].str);
    printf("%s wrmsr <cpu_id> <msr_id> <value>\n", argv[0].str);
    printf("%s lbr <subcommand>\n", argv[0].str);
    printf("%s context <cpu_id> <timeout_ms>\n", argv[0].str);
    return ZX_ERR_INTERNAL;
  }

  if (!strcmp(argv[1].str, "features")) {
    x86_feature_debug();
  } else if (!strcmp(argv[1].str, "rdmsr")) {
    if (argc != 4) {
      goto usage;
    }

    uint64_t val = read_msr_on_cpu((uint)argv[2].u, (uint)argv[3].u);
    printf("CPU %lu RDMSR %lxh val %lxh\n", argv[2].u, argv[3].u, val);
  } else if (!strcmp(argv[1].str, "wrmsr")) {
    if (argc != 5) {
      goto usage;
    }

    printf("CPU %lu WRMSR %lxh val %lxh\n", argv[2].u, argv[3].u, argv[4].u);
    write_msr_on_cpu((uint)argv[2].u, (uint)argv[3].u, argv[4].u);
  } else if (!strcmp(argv[1].str, "lbr")) {
    return LbrCtrl(argc, argv, flags);
  } else if (!strcmp(argv[1].str, "context")) {
    return GetContext(argc, argv, flags);
  } else {
    printf("unknown command\n");
    goto usage;
  }

  return ZX_OK;
}

STATIC_COMMAND_START
STATIC_COMMAND("cpu", "cpu test commands", &cmd_cpu)
STATIC_COMMAND_END(cpu)
