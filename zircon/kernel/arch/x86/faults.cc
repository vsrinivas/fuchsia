// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2015 Intel Corporation
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <bits.h>
#include <debug.h>
#include <lib/backtrace/global_cpu_context_exchange.h>
#include <lib/counters.h>
#include <lib/crashlog.h>
#include <lib/fit/defer.h>
#include <lib/ktrace.h>
#include <platform.h>
#include <trace.h>
#include <zircon/boot/crash-reason.h>
#include <zircon/hw/debug/x86.h>
#include <zircon/syscalls/exception.h>
#include <zircon/types.h>

#include <arch/exception.h>
#include <arch/regs.h>
#include <arch/user_copy.h>
#include <arch/x86.h>
#include <arch/x86/apic.h>
#include <arch/x86/descriptor.h>
#include <arch/x86/feature.h>
#include <arch/x86/interrupts.h>
#include <arch/x86/perf_mon.h>
#include <arch/x86/registers.h>
#include <kernel/interrupt.h>
#include <kernel/thread.h>
#include <pretty/hexdump.h>
#include <vm/fault.h>
#include <vm/vm.h>

// Returns whether the register state indicates that the CPU was executing
// userland code.
static bool is_from_user(const iframe_t* frame) { return SELECTOR_PL(frame->cs) != 0; }

static void dump_fault_frame(iframe_t* frame) {
  PrintFrame(stdout, *frame);

  // dump the bottom of the current stack
  void* stack = frame;

  if (frame->cs == CODE_64_SELECTOR) {
    printf("bottom of kernel stack at %p:\n", stack);
    hexdump(stack, 128);
  }
}

KCOUNTER(exceptions_debug, "exceptions.debug")
KCOUNTER(exceptions_nmi, "exceptions.nmi")
KCOUNTER(exceptions_brkpt, "exceptions.breakpoint")
KCOUNTER(exceptions_invop, "exceptions.inv_opcode")
KCOUNTER(exceptions_dev_na, "exceptions.dev_na")
KCOUNTER(exceptions_dfault, "exceptions.double_fault")
KCOUNTER(exceptions_fpu, "exceptions.fpu")
KCOUNTER(exceptions_simd, "exceptions.simd")
KCOUNTER(exceptions_gpf, "exceptions.gpf")
KCOUNTER(exceptions_page, "exceptions.page_fault")
KCOUNTER(exceptions_apic_err, "exceptions.apic_error")
KCOUNTER(exceptions_apic_timer, "exceptions.apic_timer")
KCOUNTER(exceptions_irq, "exceptions.irq")
KCOUNTER(exceptions_unhandled, "exceptions.unhandled")
KCOUNTER(exceptions_user, "exceptions.user")

__NO_RETURN static void exception_die(iframe_t* frame, const char* msg) {
  platform_panic_start();

  printf("vector %lu\n", (ulong)frame->vector);
  dprintf(CRITICAL, "%s", msg);
  dump_fault_frame(frame);
  g_crashlog.iframe = frame;

  // try to dump the user stack
  if (is_user_accessible(frame->user_sp)) {
    uint8_t buf[256];
    if (arch_copy_from_user(buf, (void*)frame->user_sp, sizeof(buf)) == ZX_OK) {
      printf("bottom of user stack at 0x%lx:\n", (vaddr_t)frame->user_sp);
      hexdump_ex(buf, sizeof(buf), frame->user_sp);
    }
  }

  platform_halt(HALT_ACTION_HALT, ZirconCrashReason::Panic);
}

static bool try_dispatch_user_exception(iframe_t* frame, uint exception_type) {
  if (is_from_user(frame)) {
    struct arch_exception_context context = {
        .frame = frame,
        .cr2 = 0,
        .user_synth_code = 0,
        .user_synth_data = 0,
        .is_page_fault = false,
    };
    PreemptionState& preemption_state = Thread::Current::preemption_state();

    arch_set_blocking_disallowed(false);
    arch_enable_ints();
    preemption_state.PreemptReenable();

    zx_status_t erc = dispatch_user_exception(exception_type, &context);

    preemption_state.PreemptDisable();
    arch_disable_ints();
    arch_set_blocking_disallowed(true);

    if (erc == ZX_OK)
      return true;
  }

  return false;
}

static void x86_debug_handler(iframe_t* frame) {
  // DR6 is the status register that explains what exception happened (single step, hardware
  // breakpoint, etc.).
  //
  // We only need to keep track of DR6 because the other state doesn't change and the only way
  // to actually change the debug registers for a thread is through the thread_write_state
  // syscall.

  Thread* thread = Thread::Current::Get();

  // We save the current state so that exception handlers can check what kind of exception it was.
  x86_read_debug_status(&thread->arch().debug_state.dr6);

  // NOTE: a HW breakpoint exception can also represent a single step.
  // TODO(fxbug.dev/32872): Is it worth separating this into two separate exceptions?
  if (try_dispatch_user_exception(frame, ZX_EXCP_HW_BREAKPOINT)) {
    // If the exception was successfully handled, we mask the debug the single step bit, as the cpu
    // doesn't automatically do it.
    //
    // After this point, any exception handler that reads DR6 won't see the single step bit active.
    X86_DBG_STATUS_BD_SET(&thread->arch().debug_state.dr6, 0);
    X86_DBG_STATUS_BS_SET(&thread->arch().debug_state.dr6, 0);
    X86_DBG_STATUS_BT_SET(&thread->arch().debug_state.dr6, 0);
    x86_write_debug_status(thread->arch().debug_state.dr6);

    return;
  }

  exception_die(frame, "unhandled hw breakpoint, halting\n");
}

// This is the NMI handler.  It's separate from x86_exception_handler because we
// must take care to avoid calling *any* non-reentrant-safe code that may have
// been interrupted by the NMI.  In particular, it's crucial that we don't
// acquire any spinlocks in the NMI handler because the NMI could have
// interrupted the thread while it was holding the spinlock we would then
// attempt to (re)acquire.
void x86_nmi_handler(iframe_t* frame) {
  // Generally speaking, NMIs don't "stack".  That is, further NMIs are disabled
  // until the execution of the next IRET instruction so to prevent reentrancy
  // we must take care to not execute an IRET until the NMI handler is complete.
  //
  // Keeping interrupts disabled and avoiding faults is critical because the
  // *next* IRET to execute will enable further NMIs.  Consider what might
  // happen if we enabled interrupts here.  If interrupts were enabled, a timer
  // interrupt might fire and stack the timer interrupt handler on top of this
  // NMI handler.  When the timer interrupt handler completes, and issues an
  // IRET, NMIs would be re-enabled even though this handler is still on the
  // stack.  We'd be open to unexpected reentrancy.
  DEBUG_ASSERT(arch_ints_disabled());

  kcounter_add(exceptions_nmi, 1);
  g_cpu_context_exchange.HandleRequest(frame->rbp, *frame);

  DEBUG_ASSERT(arch_ints_disabled());
}

static void x86_breakpoint_handler(iframe_t* frame) {
  if (try_dispatch_user_exception(frame, ZX_EXCP_SW_BREAKPOINT))
    return;

  exception_die(frame, "unhandled sw breakpoint, halting\n");
}

static void x86_gpf_handler(iframe_t* frame) {
  DEBUG_ASSERT(arch_ints_disabled());

  // Check if we were doing a GPF test, e.g. to check if an MSR exists.
  struct x86_percpu* percpu = x86_get_percpu();
  if (unlikely(percpu->gpf_return_target)) {
    ASSERT(!is_from_user(frame));

    // Set up return to new address
    frame->ip = percpu->gpf_return_target;
    percpu->gpf_return_target = 0;
    return;
  }

  if (try_dispatch_user_exception(frame, ZX_EXCP_GENERAL))
    return;

  exception_die(frame, "unhandled gpf, halting\n");
}

static void x86_invop_handler(iframe_t* frame) {
  if (try_dispatch_user_exception(frame, ZX_EXCP_UNDEFINED_INSTRUCTION))
    return;

  exception_die(frame, "invalid opcode, halting\n");
}

static void x86_df_handler(iframe_t* frame) {
  // Do not give the user exception handler the opportunity to handle double
  // faults, since they indicate an unexpected system state and cannot be
  // recovered from.
  kcounter_add(exceptions_dfault, 1);
  exception_die(frame, "double fault, halting\n");
}

static void x86_unhandled_exception(iframe_t* frame) {
  if (try_dispatch_user_exception(frame, ZX_EXCP_GENERAL))
    return;

  exception_die(frame, "unhandled exception, halting\n");
}

static void x86_dump_pfe(iframe_t* frame, ulong cr2) {
  uint64_t error_code = frame->err_code;

  vaddr_t v_addr = cr2;
  vaddr_t ssp = frame->user_ss & X86_8BYTE_MASK;
  vaddr_t sp = frame->user_sp;
  vaddr_t cs = frame->cs & X86_8BYTE_MASK;
  vaddr_t ip = frame->ip;

  dprintf(CRITICAL, "<PAGE FAULT> Instruction Pointer   = 0x%lx:0x%lx\n", (ulong)cs, (ulong)ip);
  dprintf(CRITICAL, "<PAGE FAULT> Stack Pointer         = 0x%lx:0x%lx\n", (ulong)ssp, (ulong)sp);
  dprintf(CRITICAL, "<PAGE FAULT> Fault Linear Address  = 0x%lx\n", (ulong)v_addr);
  dprintf(CRITICAL, "<PAGE FAULT> Error Code Value      = 0x%lx\n", (ulong)error_code);
  dprintf(CRITICAL, "<PAGE FAULT> Error Code Type       = %s %s %s%s, %s\n",
          error_code & PFEX_U ? "user" : "supervisor", error_code & PFEX_W ? "write" : "read",
          error_code & PFEX_I ? "instruction" : "data", error_code & PFEX_RSV ? " rsv" : "",
          error_code & PFEX_P ? "protection violation" : "page not present");
}

__NO_RETURN static void x86_fatal_pfe_handler(iframe_t* frame, ulong cr2) {
  x86_dump_pfe(frame, cr2);

  uint64_t error_code = frame->err_code;

  dump_thread_during_panic(Thread::Current::Get(), true);

  if (error_code & PFEX_U) {
    // User mode page fault
    switch (error_code) {
      case 4:
      case 5:
      case 6:
      case 7:
        exception_die(frame, "User Page Fault exception, halting\n");
        break;
    }
  } else {
    // Supervisor mode page fault
    switch (error_code) {
      case 0:
      case 1:
      case 2:
      case 3:
        exception_die(frame, "Supervisor Page Fault exception, halting\n");
        break;
    }
  }

  exception_die(frame, "unhandled page fault, halting\n");
}

static zx_status_t x86_pfe_handler(iframe_t* frame) {
  /* Handle a page fault exception */
  uint64_t error_code = frame->err_code;
  vaddr_t va = x86_get_cr2();

  uint64_t pfr = Thread::Current::Get()->arch().page_fault_resume;
  if (unlikely(!(error_code & PFEX_U))) {
    // Any page fault in kernel mode that's not during user-copy is a bug.
    // Check for an SMAP violation.
    //
    // By policy, the kernel is not allowed to access user memory except when
    // performing a user_copy. SMAP is used to enforce the policy.
    if (g_x86_feature_has_smap &&          // CPU supports SMAP
        !(frame->flags & X86_FLAGS_AC) &&  // SMAP was enabled at time of fault
        is_user_accessible(va)) {          // fault address is a user address
      printf("x86_pfe_handler: potential SMAP failure, supervisor access at address %#" PRIxPTR
             "\n",
             va);
      pfr = 0;
    }
    if (unlikely(!pfr)) {
      exception_die(frame, "page fault in kernel mode\n");
    }
  }

  /* reenable interrupts */
  PreemptionState& preemption_state = Thread::Current::preemption_state();
  arch_set_blocking_disallowed(false);
  arch_enable_ints();
  preemption_state.PreemptReenable();

  /* make sure we put interrupts back as we exit */
  auto cleanup = fit::defer([&preemption_state]() {
    preemption_state.PreemptDisable();
    arch_disable_ints();
    arch_set_blocking_disallowed(true);
  });

  /* check for flags we're not prepared to handle */
  if (unlikely(error_code & ~(PFEX_I | PFEX_U | PFEX_W | PFEX_P))) {
    printf("x86_pfe_handler: unhandled error code bits set, error code %#" PRIx64 "\n", error_code);
    return ZX_ERR_NOT_SUPPORTED;
  }

  /* convert the PF error codes to page fault flags */
  uint flags = 0;
  flags |= (error_code & PFEX_W) ? VMM_PF_FLAG_WRITE : 0;
  flags |= (error_code & PFEX_U) ? VMM_PF_FLAG_USER : 0;
  flags |= (error_code & PFEX_I) ? VMM_PF_FLAG_INSTRUCTION : 0;
  flags |= (error_code & PFEX_P) ? 0 : VMM_PF_FLAG_NOT_PRESENT;

  /* Check if the page fault handler should be skipped. It is skipped if there's a page_fault_resume
   * address and the highest bit is 0. */
  if (unlikely(pfr && !BIT_SET(pfr, X86_PFR_RUN_FAULT_HANDLER_BIT))) {
    // Need to reconstruct the canonical resume address by ensuring it is correctly sign extended.
    // Double check the bit before X86_PFR_RUN_FAULT_HANDLER_BIT was set (indicating kernel
    // address) and fill it in.
    DEBUG_ASSERT(BIT_SET(pfr, X86_PFR_RUN_FAULT_HANDLER_BIT - 1));
    frame->ip = pfr | (1ull << X86_PFR_RUN_FAULT_HANDLER_BIT);
    frame->rdx = va;
    frame->rcx = flags;
    return ZX_OK;
  }

  /* call the high level page fault handler */
  zx_status_t pf_err = vmm_page_fault_handler(va, flags);
  if (likely(pf_err == ZX_OK))
    return ZX_OK;

  /* if the high level page fault handler can't deal with it,
   * resort to trying to recover first, before bailing */

  /* Check if a resume address is specified, and just return to it if so */
  if (unlikely(pfr)) {
    // Having the X86_PFR_RUN_FAULT_HANDLER_BIT set should have already resulted in a valid
    // sign extended canonical address. Double check the bit before, which should be a one.
    DEBUG_ASSERT(BIT_SET(pfr, X86_PFR_RUN_FAULT_HANDLER_BIT - 1));
    frame->ip = pfr;
    return ZX_OK;
  }

  /* let high level code deal with this */
  if (is_from_user(frame)) {
    kcounter_add(exceptions_user, 1);
    struct arch_exception_context context = {
        .frame = frame,
        .cr2 = va,
        .user_synth_code = static_cast<uint32_t>(pf_err),
        .user_synth_data = 0,
        .is_page_fault = true,
    };
    return dispatch_user_exception(ZX_EXCP_FATAL_PAGE_FAULT, &context);
  }

  /* fall through to fatal path */
  return ZX_ERR_NOT_SUPPORTED;
}

static void handle_exception_types(iframe_t* frame) {
  switch (frame->vector) {
    case X86_INT_DEBUG:
      kcounter_add(exceptions_debug, 1);
      x86_debug_handler(frame);
      break;
    case X86_INT_BREAKPOINT:
      kcounter_add(exceptions_brkpt, 1);
      x86_breakpoint_handler(frame);
      break;

    case X86_INT_INVALID_OP:
      kcounter_add(exceptions_invop, 1);
      x86_invop_handler(frame);
      break;

    case X86_INT_DEVICE_NA:
      kcounter_add(exceptions_dev_na, 1);
      exception_die(frame, "device na fault\n");
      break;

    case X86_INT_DOUBLE_FAULT:
      x86_df_handler(frame);
      break;
    case X86_INT_FPU_FP_ERROR:
      kcounter_add(exceptions_fpu, 1);
      x86_unhandled_exception(frame);
      break;
    case X86_INT_SIMD_FP_ERROR:
      kcounter_add(exceptions_simd, 1);
      x86_unhandled_exception(frame);
      break;
    case X86_INT_GP_FAULT:
      kcounter_add(exceptions_gpf, 1);
      x86_gpf_handler(frame);
      break;

    case X86_INT_PAGE_FAULT:
      kcounter_add(exceptions_page, 1);
      CPU_STATS_INC(page_faults);
      if (x86_pfe_handler(frame) != ZX_OK)
        x86_fatal_pfe_handler(frame, x86_get_cr2());
      break;

    /* ignore spurious APIC irqs */
    case X86_INT_APIC_SPURIOUS:
      break;
    case X86_INT_APIC_ERROR: {
      kcounter_add(exceptions_apic_err, 1);
      apic_error_interrupt_handler();
      apic_issue_eoi();
      break;
    }
    case X86_INT_APIC_TIMER: {
      kcounter_add(exceptions_apic_timer, 1);
      apic_timer_interrupt_handler();
      apic_issue_eoi();
      break;
    }
    case X86_INT_IPI_GENERIC: {
      mp_mbx_generic_irq(nullptr);
      apic_issue_eoi();
      break;
    }
    case X86_INT_IPI_RESCHEDULE: {
      mp_mbx_reschedule_irq(nullptr);
      apic_issue_eoi();
      break;
    }
    case X86_INT_IPI_INTERRUPT: {
      mp_mbx_interrupt_irq(nullptr);
      apic_issue_eoi();
      break;
    }
    case X86_INT_IPI_HALT: {
      x86_ipi_halt_handler(nullptr);
      /* no return */
      break;
    }
    case X86_INT_APIC_PMI: {
      apic_pmi_interrupt_handler(frame);
      // Note: apic_pmi_interrupt_handler calls apic_issue_eoi().
      break;
    }
    /* pass all other non-Intel defined irq vectors to the platform */
    case X86_INT_PLATFORM_BASE ... X86_INT_PLATFORM_MAX: {
      kcounter_add(exceptions_irq, 1);
      platform_irq(frame);
      break;
    }

    /* Integer division-by-zero */
    case X86_INT_DIVIDE_0:
    /* Overflow for INTO instruction (should be x86-32-only) */
    case X86_INT_OVERFLOW:
    /* Bound range exceeded for BOUND instruction (should be x86-32-only) */
    case X86_INT_BOUND_RANGE:
    /* Loading segment with "not present" bit set */
    case X86_INT_SEGMENT_NOT_PRESENT:
    /* Stack segment fault (should be x86-32-only) */
    case X86_INT_STACK_FAULT:
    /* Misaligned memory access when AC=1 in flags */
    case X86_INT_ALIGNMENT_CHECK:
      kcounter_add(exceptions_unhandled, 1);
      x86_unhandled_exception(frame);
      break;

    default:
      exception_die(frame, "unhandled exception type, halting\n");
      break;
  }
}

/* top level x86 exception handler for most exceptions and irqs */
void x86_exception_handler(iframe_t* frame) {
  // NMIs should be handled in a different handler.
  DEBUG_ASSERT(frame->vector != X86_INT_NMI);

  // are we recursing?
  if (unlikely(arch_blocking_disallowed())) {
    exception_die(frame, "recursion in interrupt handler\n");
  }

  int_handler_saved_state_t state;
  int_handler_start(&state);

  // did we come from user or kernel space?
  bool from_user = is_from_user(frame);

  const auto entry_vector = frame->vector;
  if (entry_vector != X86_INT_PAGE_FAULT && unlikely(ktrace_tag_enabled(TAG_IRQ_ENTER))) {
    // For page faults, the cpu number for the IRQ_ENTER event might be different from the IRQ_EXIT
    // event. A context switch can occur if the page fault is fulfilled asynchronously by a pager.
    // Hence page fault events are emitted in the thread context, not the cpu context like other
    // irq's. See TAG_PAGE_FAULT in vmm_page_fault_handler().
    fxt::Argument<fxt::ArgumentType::kUint64, fxt::RefType::kId> arg(
        fxt::StringRef("irq #"_stringref->GetFxtId()), entry_vector);
    fxt_duration_begin(TAG_IRQ_ENTER, current_ticks(),
                       fxt::ThreadRef(kNoProcess, kKernelPseudoCpuBase + arch_curr_cpu_num()),
                       fxt::StringRef("kernel:irq"_stringref->GetFxtId()),
                       fxt::StringRef("irq"_stringref->GetFxtId()), arg);
  }

  // deliver the interrupt
  handle_exception_types(frame);

  if (entry_vector != X86_INT_PAGE_FAULT && unlikely(ktrace_tag_enabled(TAG_IRQ_EXIT))) {
    fxt::Argument<fxt::ArgumentType::kUint64, fxt::RefType::kId> arg(
        fxt::StringRef("irq #"_stringref->GetFxtId()), entry_vector);
    fxt_duration_end(TAG_IRQ_EXIT, current_ticks(),
                     fxt::ThreadRef(kNoProcess, kKernelPseudoCpuBase + arch_curr_cpu_num()),
                     fxt::StringRef("kernel:irq"_stringref->GetFxtId()),
                     fxt::StringRef("irq"_stringref->GetFxtId()), arg);
  }

  bool do_preempt = int_handler_finish(&state);

  /* if we came from user space, check to see if we have any signals to handle */
  if (unlikely(from_user)) {
    /* in the case of receiving a kill signal, this function may not return,
     * but the scheduler would have been invoked so it's fine.
     */
    arch_iframe_process_pending_signals(frame);
  }

  if (do_preempt) {
    Thread::Current::Preempt();
  }

  DEBUG_ASSERT_MSG(arch_ints_disabled(),
                   "ints disabled on way out of exception, vector %" PRIu64 " IP %#" PRIx64 "\n",
                   frame->vector, frame->ip);
}

void x86_syscall_process_pending_signals(syscall_regs_t* gregs) {
  Thread::Current::ProcessPendingSignals(GeneralRegsSource::Syscall, gregs);
}

void arch_iframe_process_pending_signals(iframe_t* iframe) {
  DEBUG_ASSERT(iframe != nullptr);
  Thread::Current::ProcessPendingSignals(GeneralRegsSource::Iframe, iframe);
}

void arch_dump_exception_context(const arch_exception_context_t* context) {
  // If we don't have a frame, there's nothing more we can print.
  if (context->frame == nullptr) {
    printf("no frame to dump\n");
    return;
  }

  if (context->is_page_fault) {
    x86_dump_pfe(context->frame, context->cr2);
  }

  dump_fault_frame(context->frame);

  // try to dump the user stack
  if (context->frame->cs != CODE_64_SELECTOR && is_user_accessible(context->frame->user_sp)) {
    uint8_t buf[256];
    if (arch_copy_from_user(buf, (void*)context->frame->user_sp, sizeof(buf)) == ZX_OK) {
      printf("bottom of user stack at 0x%lx:\n", (vaddr_t)context->frame->user_sp);
      hexdump_ex(buf, sizeof(buf), context->frame->user_sp);
    }
  }
}

void arch_fill_in_exception_context(const arch_exception_context_t* arch_context,
                                    zx_exception_report_t* report) {
  zx_exception_context_t* zx_context = &report->context;

  zx_context->synth_code = arch_context->user_synth_code;
  zx_context->synth_data = arch_context->user_synth_data;

  // TODO(fxbug.dev/30521): |frame| will be nullptr for synthetic exceptions that
  // don't provide general register values yet.
  if (arch_context->frame) {
    zx_context->arch.u.x86_64.vector = arch_context->frame->vector;
    zx_context->arch.u.x86_64.err_code = arch_context->frame->err_code;
  }
  zx_context->arch.u.x86_64.cr2 = arch_context->cr2;
}

zx_status_t arch_dispatch_user_policy_exception(uint32_t policy_exception_code,
                                                uint32_t policy_exception_data) {
  arch_exception_context_t context = {};
  context.user_synth_code = policy_exception_code;
  context.user_synth_data = policy_exception_data;
  return dispatch_user_exception(ZX_EXCP_POLICY_ERROR, &context);
}

bool arch_install_exception_context(Thread* thread, const arch_exception_context_t* context) {
  if (!context->frame) {
    // TODO(fxbug.dev/30521): Must be a synthetic exception as they don't (yet) provide the
    // registers.
    return false;
  }

  arch_set_suspended_general_regs(thread, GeneralRegsSource::Iframe, context->frame);
  return true;
}

void arch_remove_exception_context(Thread* thread) { arch_reset_suspended_general_regs(thread); }
