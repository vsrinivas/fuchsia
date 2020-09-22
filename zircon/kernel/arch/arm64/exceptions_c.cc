// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include <bits.h>
#include <debug.h>
#include <inttypes.h>
#include <lib/counters.h>
#include <lib/crashlog.h>
#include <platform.h>
#include <stdio.h>
#include <trace.h>
#include <zircon/syscalls/exception.h>
#include <zircon/types.h>

#include <arch/arch_ops.h>
#include <arch/arm64.h>
#include <arch/arm64/uarch.h>
#include <arch/exception.h>
#include <arch/thread.h>
#include <arch/user_copy.h>
#include <kernel/interrupt.h>
#include <kernel/thread.h>
#include <pretty/hexdump.h>
#include <vm/fault.h>
#include <vm/vm.h>

#define LOCAL_TRACE 0

#define DFSC_ALIGNMENT_FAULT 0b100001

static void dump_iframe(const arm64_iframe_t* iframe) {
  printf("iframe %p:\n", iframe);
  printf("x0  %#18" PRIx64 " x1  %#18" PRIx64 " x2  %#18" PRIx64 " x3  %#18" PRIx64 "\n",
         iframe->r[0], iframe->r[1], iframe->r[2], iframe->r[3]);
  printf("x4  %#18" PRIx64 " x5  %#18" PRIx64 " x6  %#18" PRIx64 " x7  %#18" PRIx64 "\n",
         iframe->r[4], iframe->r[5], iframe->r[6], iframe->r[7]);
  printf("x8  %#18" PRIx64 " x9  %#18" PRIx64 " x10 %#18" PRIx64 " x11 %#18" PRIx64 "\n",
         iframe->r[8], iframe->r[9], iframe->r[10], iframe->r[11]);
  printf("x12 %#18" PRIx64 " x13 %#18" PRIx64 " x14 %#18" PRIx64 " x15 %#18" PRIx64 "\n",
         iframe->r[12], iframe->r[13], iframe->r[14], iframe->r[15]);
  printf("x16 %#18" PRIx64 " x17 %#18" PRIx64 " x18 %#18" PRIx64 " x19 %#18" PRIx64 "\n",
         iframe->r[16], iframe->r[17], iframe->r[18], iframe->r[19]);
  printf("x20 %#18" PRIx64 " x21 %#18" PRIx64 " x22 %#18" PRIx64 " x23 %#18" PRIx64 "\n",
         iframe->r[20], iframe->r[21], iframe->r[22], iframe->r[23]);
  printf("x24 %#18" PRIx64 " x25 %#18" PRIx64 " x26 %#18" PRIx64 " x27 %#18" PRIx64 "\n",
         iframe->r[24], iframe->r[25], iframe->r[26], iframe->r[27]);
  printf("x28 %#18" PRIx64 " x29 %#18" PRIx64 " lr  %#18" PRIx64 " usp %#18" PRIx64 "\n",
         iframe->r[28], iframe->r[29], iframe->lr, iframe->usp);
  printf("elr  %#18" PRIx64 "\n", iframe->elr);
  printf("spsr %#18" PRIx64 "\n", iframe->spsr);
}

// clang-format off
static const char* dfsc_to_string(uint32_t dfsc) {
  switch (dfsc) {
    case 0b000000: return "Address Size Fault, Level 0";
    case 0b000001: return "Address Size Fault, Level 1";
    case 0b000010: return "Address Size Fault, Level 2";
    case 0b000011: return "Address Size Fault, Level 3";
    case 0b000100: return "Translation Fault, Level 0";
    case 0b000101: return "Translation Fault, Level 1";
    case 0b000110: return "Translation Fault, Level 2";
    case 0b000111: return "Translation Fault, Level 3";
    case 0b001001: return "Access Flag Fault, Level 1";
    case 0b001010: return "Access Flag Fault, Level 2";
    case 0b001011: return "Access Flag Fault, Level 3";
    case 0b001101: return "Permission Fault, Level 1";
    case 0b001110: return "Permission Fault, Level 2";
    case 0b001111: return "Permission Fault, Level 3";
    case 0b010000: return "Synchronous External Abort";
    case 0b010001: return "Synchronous Tag Check Fail";
    case 0b010100: return "Synchronous External Abort, Level 0";
    case 0b010101: return "Synchronous External Abort, Level 1";
    case 0b010110: return "Synchronous External Abort, Level 2";
    case 0b010111: return "Synchronous External Abort, Level 3";
    case 0b011000: return "Synchronous Parity or ECC Abort";
    case 0b011100: return "Synchronous Parity or ECC Abort, Level 0";
    case 0b011101: return "Synchronous Parity or ECC Abort, Level 1";
    case 0b011110: return "Synchronous Parity or ECC Abort, Level 2";
    case 0b011111: return "Synchronous Parity or ECC Abort, Level 3";
    case 0b100001: return "Alignment Fault";
    case 0b110000: return "TLB Conflict Abort";
    case 0b110100: return "Implementation Defined, Lockdown";
    case 0b110101: return "Implementation Defined, Unsupported exclusive or atomic";
    case 0b111101: return "Section Domain Fault";
    case 0b111110: return "Page Domain Fault";
    default: return "Unknown";
  }
}
// clang-format on

KCOUNTER(exceptions_brkpt, "exceptions.breakpoint")
KCOUNTER(exceptions_hw_brkpt, "exceptions.hw_breakpoint")
KCOUNTER(exceptions_hw_wp, "exceptions.hw_watchpoint")
KCOUNTER(exceptions_fpu, "exceptions.fpu")
KCOUNTER(exceptions_page, "exceptions.page_fault")
KCOUNTER(exceptions_irq, "exceptions.irq")
KCOUNTER(exceptions_unhandled, "exceptions.unhandled")
KCOUNTER(exceptions_user, "exceptions.user")
KCOUNTER(exceptions_unknown, "exceptions.unknown")
KCOUNTER(exceptions_access, "exceptions.access_fault")

static zx_status_t try_dispatch_user_data_fault_exception(zx_excp_type_t type,
                                                          arm64_iframe_t* iframe, uint32_t esr,
                                                          uint64_t far) {
  arch_exception_context_t context = {};
  DEBUG_ASSERT(iframe != nullptr);
  context.frame = iframe;
  context.esr = esr;
  context.far = far;

  arch_enable_ints();
  zx_status_t status = dispatch_user_exception(type, &context);
  arch_disable_ints();
  return status;
}

static zx_status_t try_dispatch_user_exception(zx_excp_type_t type, arm64_iframe_t* iframe,
                                               uint32_t esr) {
  return try_dispatch_user_data_fault_exception(type, iframe, esr, 0);
}

// Prints exception details and then panics.
__NO_RETURN static void exception_die(arm64_iframe_t* iframe, uint32_t esr, uint64_t far,
                                      const char* format, ...) {
  platform_panic_start();

  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);

  uint32_t ec = BITS_SHIFT(esr, 31, 26);
  uint32_t il = BIT(esr, 25);
  uint32_t iss = BITS(esr, 24, 0);

  /* fatal exception, die here */
  printf("ESR %#x: ec %#x, il %#x, iss %#x\n", esr, ec, il, iss);
  dump_iframe(iframe);
  crashlog.iframe = iframe;
  crashlog.esr = esr;
  crashlog.far = far;

  platform_halt(HALT_ACTION_HALT, ZirconCrashReason::Panic);
}

static void arm64_unknown_handler(arm64_iframe_t* iframe, uint exception_flags, uint32_t esr) {
  /* this is for a lot of reasons, but most of them are undefined instructions */
  if (unlikely((exception_flags & ARM64_EXCEPTION_FLAG_LOWER_EL) == 0)) {
    /* trapped inside the kernel, this is bad */
    exception_die(iframe, esr, __arm_rsr64("far_el1"),
                  "unknown exception in kernel: PC at %#" PRIx64 "\n", iframe->elr);
  }
  try_dispatch_user_exception(ZX_EXCP_UNDEFINED_INSTRUCTION, iframe, esr);
}

static void arm64_brk_handler(arm64_iframe_t* iframe, uint exception_flags, uint32_t esr) {
  if (unlikely((exception_flags & ARM64_EXCEPTION_FLAG_LOWER_EL) == 0)) {
    /* trapped inside the kernel, this is bad */
    exception_die(iframe, esr, __arm_rsr64("far_el1"), "BRK in kernel: PC at %#" PRIx64 "\n",
                  iframe->elr);
  }
  // Spectre V2: If we took a BRK exception from EL0, but the ELR address is not a user address,
  // invalidate the branch predictor. User code may be attempting to mistrain indirect branch
  // prediction structures.
  if (unlikely(!is_user_address(iframe->elr)) && arm64_uarch_needs_spectre_v2_mitigation()) {
    arm64_uarch_do_spectre_v2_mitigation();
  }
  try_dispatch_user_exception(ZX_EXCP_SW_BREAKPOINT, iframe, esr);
}

static void arm64_hw_breakpoint_exception_handler(arm64_iframe_t* iframe, uint exception_flags,
                                                  uint32_t esr) {
  if (unlikely((exception_flags & ARM64_EXCEPTION_FLAG_LOWER_EL) == 0)) {
    /* trapped inside the kernel, this is bad */
    exception_die(iframe, esr, __arm_rsr64("far_el1"),
                  "HW breakpoint in kernel: PC at %#" PRIx64 "\n", iframe->elr);
  }

  // We don't need to save the debug state because it doesn't change by an exception. The only
  // way to change the debug state is through the thread write syscall.

  // NOTE: ARM64 Doesn't provide a good way to comunicate exception status (without exposing ESR
  //       to userspace). This means a debugger will have to compare the registers with the PC
  //       on the exceptions to find out which breakpoint triggered the exception.
  try_dispatch_user_exception(ZX_EXCP_HW_BREAKPOINT, iframe, esr);
}

static void arm64_watchpoint_exception_handler(arm64_iframe_t* iframe, uint exception_flags,
                                               uint32_t esr) {
  // Arm64 uses the Fault Address Register to determine which watchpoint triggered the exception.
  uint64_t far = __arm_rsr64("far_el1");

  if (unlikely((exception_flags & ARM64_EXCEPTION_FLAG_LOWER_EL) == 0)) {
    /* trapped inside the kernel, this is bad */
    exception_die(iframe, esr, far, "Watchpoint in kernel: PC at %#" PRIx64 "\n", iframe->elr);
  }

  // We don't need to save the debug state because it doesn't change by an exception. The only
  // way to change the debug state is through the thread write syscall.

  try_dispatch_user_data_fault_exception(ZX_EXCP_HW_BREAKPOINT, iframe, esr, far);
}

static void arm64_step_handler(arm64_iframe_t* iframe, uint exception_flags, uint32_t esr) {
  if (unlikely((exception_flags & ARM64_EXCEPTION_FLAG_LOWER_EL) == 0)) {
    /* trapped inside the kernel, this is bad */
    exception_die(iframe, esr, __arm_rsr64("far_el1"),
                  "software step in kernel: PC at %#" PRIx64 "\n", iframe->elr);
  }
  // TODO(fxbug.dev/32872): Is it worth separating this into two separate exceptions?
  try_dispatch_user_exception(ZX_EXCP_HW_BREAKPOINT, iframe, esr);
}

static void arm64_fpu_handler(arm64_iframe_t* iframe, uint exception_flags, uint32_t esr) {
  if (unlikely((exception_flags & ARM64_EXCEPTION_FLAG_LOWER_EL) == 0)) {
    /* we trapped a floating point instruction inside our own EL, this is bad */
    exception_die(iframe, esr, __arm_rsr64("far_el1"),
                  "invalid fpu use in kernel: PC at %#" PRIx64 "\n", iframe->elr);
  }
  arm64_fpu_exception(iframe, exception_flags);
}

static void arm64_instruction_abort_handler(arm64_iframe_t* iframe, uint exception_flags,
                                            uint32_t esr) {
  /* read the FAR register */
  uint64_t far = __arm_rsr64("far_el1");
  uint32_t ec = BITS_SHIFT(esr, 31, 26);
  uint32_t iss = BITS(esr, 24, 0);
  bool is_user = !BIT(ec, 0);

  // Spectre V2: If we took an instruction abort in EL0 but the faulting address is not a user
  // address, invalidate the branch predictor. The $PC may have been updated before the abort is
  // delivered, user code may be attempting to mistrain indirect branch prediction structures.
  if (unlikely(is_user && !is_user_address(far)) && arm64_uarch_needs_spectre_v2_mitigation()) {
    arm64_uarch_do_spectre_v2_mitigation();
  }

  uint pf_flags = VMM_PF_FLAG_INSTRUCTION;
  pf_flags |= is_user ? VMM_PF_FLAG_USER : 0;
  /* Check if this was not permission fault */
  if ((iss & 0b111100) != 0b001100) {
    pf_flags |= VMM_PF_FLAG_NOT_PRESENT;
  }

  LTRACEF("instruction abort: PC at %#" PRIx64 ", is_user %d, FAR %" PRIx64 ", esr %#x, iss %#x\n",
          iframe->elr, is_user, far, esr, iss);

  arch_enable_ints();
  zx_status_t err;
  // Check for accessed fault separately and use the dedicated handler.
  if ((iss & 0b111100) == 0b001000) {
    exceptions_access.Add(1);
    err = vmm_accessed_fault_handler(far);
  } else {
    kcounter_add(exceptions_page, 1);
    CPU_STATS_INC(page_faults);
    err = vmm_page_fault_handler(far, pf_flags);
  }
  arch_disable_ints();
  if (err >= 0) {
    return;
  }

  // If this is from user space, let the user exception handler
  // get a shot at it.
  if (is_user) {
    kcounter_add(exceptions_user, 1);
    if (try_dispatch_user_data_fault_exception(ZX_EXCP_FATAL_PAGE_FAULT, iframe, esr, far) ==
        ZX_OK) {
      return;
    }
  }

  exception_die(iframe, esr, far,
                "instruction abort: PC at %#" PRIx64 ", is_user %d, FAR %" PRIx64 "\n", iframe->elr,
                is_user, far);
}

static void arm64_data_abort_handler(arm64_iframe_t* iframe, uint exception_flags, uint32_t esr) {
  /* read the FAR register */
  uint64_t far = __arm_rsr64("far_el1");
  uint32_t ec = BITS_SHIFT(esr, 31, 26);
  uint32_t iss = BITS(esr, 24, 0);
  bool is_user = !BIT(ec, 0);
  bool WnR = BIT(iss, 6);  // Write not Read
  bool CM = BIT(iss, 8);   // cache maintenance op

  uint pf_flags = 0;
  // if it was marked Write but the cache maintenance bit was set, treat it as read
  pf_flags |= (WnR && !CM) ? VMM_PF_FLAG_WRITE : 0;
  pf_flags |= is_user ? VMM_PF_FLAG_USER : 0;
  /* Check if this was not permission fault */
  if ((iss & 0b111100) != 0b001100) {
    pf_flags |= VMM_PF_FLAG_NOT_PRESENT;
  }

  LTRACEF("data fault: PC at %#" PRIx64 ", is_user %d, FAR %#" PRIx64 ", esr %#x, iss %#x\n",
          iframe->elr, is_user, far, esr, iss);

  uint64_t dfr = Thread::Current::Get()->arch().data_fault_resume;
  if (unlikely(dfr && !BIT_SET(dfr, ARM64_DFR_RUN_FAULT_HANDLER_BIT))) {
    // Need to reconstruct the canonical resume address by ensuring it is correctly sign extended.
    // Double check the bit before ARM64_DFR_RUN_FAULT_HANDLER_BIT was set (indicating kernel
    // address) and fill it in.
    DEBUG_ASSERT(BIT_SET(dfr, ARM64_DFR_RUN_FAULT_HANDLER_BIT - 1));
    iframe->elr = dfr | (1ull << ARM64_DFR_RUN_FAULT_HANDLER_BIT);
    iframe->r[1] = far;
    iframe->r[2] = pf_flags;
    return;
  }

  uint32_t dfsc = BITS(iss, 5, 0);
  // Only invoke the page fault handler for translation, access and permission faults. Any other
  // kind of fault cannot be resolved by the handler.
  // 0b0001XX is translation faults
  // 0b0010XX is access faults
  // 0b0011XX is permission faults
  if (likely((dfsc & 0b001100) != 0 && (dfsc & 0b110000) == 0)) {
    arch_enable_ints();
    zx_status_t err;
    // Send accessed faults to the separate dedicated handler.
    if ((dfsc & 0b001100) == 0b001000) {
      exceptions_access.Add(1);
      err = vmm_accessed_fault_handler(far);
    } else {
      kcounter_add(exceptions_page, 1);
      err = vmm_page_fault_handler(far, pf_flags);
    }
    arch_disable_ints();
    if (err >= 0) {
      return;
    }
  }

  // Check if the current thread was expecting a data fault and
  // we should return to its handler.
  if (dfr && is_user_address(far)) {
    // Having the ARM64_DFR_RUN_FAULT_HANDLER_BIT set should have already resulted in a valid
    // sign extended canonical address. Double check the bit before, which should be a one.
    DEBUG_ASSERT(BIT_SET(dfr, ARM64_DFR_RUN_FAULT_HANDLER_BIT - 1));
    iframe->elr = dfr;
    return;
  }

  // If this is from user space, let the user exception handler
  // get a shot at it.
  if (is_user) {
    kcounter_add(exceptions_user, 1);
    zx_excp_type_t excp_type = ZX_EXCP_FATAL_PAGE_FAULT;
    if (unlikely(dfsc == DFSC_ALIGNMENT_FAULT)) {
      excp_type = ZX_EXCP_UNALIGNED_ACCESS;
    }
    if (try_dispatch_user_data_fault_exception(excp_type, iframe, esr, far) == ZX_OK) {
      return;
    }
  }

  // Print the data fault and stop the kernel.
  exception_die(iframe, esr, far,
                "data fault: PC at %#" PRIx64 ", FAR %#" PRIx64
                "\n"
                "ISS %#x (WnR %d CM %d)\n"
                "DFSC %#x (%s)\n",
                iframe->elr, far, iss, WnR, CM, dfsc, dfsc_to_string(dfsc));
}

static inline void fix_exception_percpu_pointer(uint32_t exception_flags, uint64_t* regs) {
  // If we're returning to kernel space, make sure we restore the correct
  // per-CPU pointer to the fixed register.
  // TODO: move this fixup to the assembly glue that wraps the C irq/exception code
  if ((exception_flags & ARM64_EXCEPTION_FLAG_LOWER_EL) == 0) {
    regs[15] = (uint64_t)arm64_read_percpu_ptr();
  }
}

/* called from assembly */
extern "C" void arm64_sync_exception(arm64_iframe_t* iframe, uint exception_flags, uint32_t esr) {
  uint32_t ec = BITS_SHIFT(esr, 31, 26);

  switch (ec) {
    case 0b000000: /* unknown reason */
      kcounter_add(exceptions_unknown, 1);
      arm64_unknown_handler(iframe, exception_flags, esr);
      break;
    case 0b000111: /* floating point */
      kcounter_add(exceptions_fpu, 1);
      arm64_fpu_handler(iframe, exception_flags, esr);
      break;
    case 0b010001: /* syscall from arm32 */
    case 0b010101: /* syscall from arm64 */
      exception_die(iframe, esr, __arm_rsr64("far_el1"),
                    "syscalls should be handled in assembly\n");
      break;
    case 0b100000: /* instruction abort from lower level */
    case 0b100001: /* instruction abort from same level */
      arm64_instruction_abort_handler(iframe, exception_flags, esr);
      break;
    case 0b100100: /* data abort from lower level */
    case 0b100101: /* data abort from same level */
      arm64_data_abort_handler(iframe, exception_flags, esr);
      break;
    case 0b110000: /* HW breakpoint from a lower level */
    case 0b110001: /* HW breakpoint from same level */
      kcounter_add(exceptions_hw_brkpt, 1);
      arm64_hw_breakpoint_exception_handler(iframe, exception_flags, esr);
      break;
    case 0b110010: /* software step from lower level */
    case 0b110011: /* software step from same level */
      arm64_step_handler(iframe, exception_flags, esr);
      break;
    case 0b110100: /* HW watchpoint from a lower level */
    case 0b110101: /* HW watchpoint from same level */
      kcounter_add(exceptions_hw_wp, 1);
      arm64_watchpoint_exception_handler(iframe, exception_flags, esr);
      break;
    case 0b111000: /* BRK from arm32 */
    case 0b111100: /* BRK from arm64 */
      kcounter_add(exceptions_brkpt, 1);
      arm64_brk_handler(iframe, exception_flags, esr);
      break;
    default: {
      /* TODO: properly decode more of these */
      if (unlikely((exception_flags & ARM64_EXCEPTION_FLAG_LOWER_EL) == 0)) {
        /* trapped inside the kernel, this is bad */
        exception_die(iframe, esr, __arm_rsr64("far_el1"),
                      "unhandled exception in kernel: PC at %#" PRIx64 "\n", iframe->elr);
      }
      /* let the user exception handler get a shot at it */
      kcounter_add(exceptions_unhandled, 1);
      if (try_dispatch_user_exception(ZX_EXCP_GENERAL, iframe, esr) == ZX_OK) {
        break;
      }
      exception_die(iframe, esr, __arm_rsr64("far_el1"), "unhandled synchronous exception\n");
    }
  }

  /* if we came from user space, check to see if we have any signals to handle */
  if (exception_flags & ARM64_EXCEPTION_FLAG_LOWER_EL) {
    /* in the case of receiving a kill signal, this function may not return,
     * but the scheduler would have been invoked so it's fine.
     */
    arch_iframe_process_pending_signals(iframe);
  }

  fix_exception_percpu_pointer(exception_flags, iframe->r);
}

/* called from assembly */
extern "C" void arm64_irq(iframe_t* iframe, uint exception_flags) {
  LTRACEF("iframe %p, flags %#x\n", iframe, exception_flags);
  bool is_user = exception_flags & ARM64_EXCEPTION_FLAG_LOWER_EL;

  // Spectre V2: If we took an interrupt while in EL0 but $PC was not a user address, invalidate
  // the branch predictor. User code may be attempting to mistrain an indirect branch predictor.
  if (unlikely(is_user && !is_user_address(iframe->elr)) &&
      arm64_uarch_needs_spectre_v2_mitigation()) {
    arm64_uarch_do_spectre_v2_mitigation();
  }

  int_handler_saved_state_t state;
  int_handler_start(&state);

  kcounter_add(exceptions_irq, 1);
  platform_irq(iframe);

  bool do_preempt = int_handler_finish(&state);

  /* if we came from user space, check to see if we have any signals to handle */
  if (is_user) {
    /* in the case of receiving a kill signal, this function may not return,
     * but the scheduler would have been invoked so it's fine.
     */
    arch_iframe_process_pending_signals(iframe);
  }

  /* preempt the thread if the interrupt has signaled it */
  if (do_preempt) {
    Thread::Current::Preempt();
  }

  fix_exception_percpu_pointer(exception_flags, iframe->r);
}

/* called from assembly */
extern "C" void arm64_invalid_exception(arm64_iframe_t* iframe, unsigned int which) {
  platform_panic_start();

  printf("invalid exception, which %#x\n", which);
  dump_iframe(iframe);

  platform_halt(HALT_ACTION_HALT, ZirconCrashReason::Panic);
}

/* called from assembly */
extern "C" void arch_iframe_process_pending_signals(iframe_t* iframe) {
  DEBUG_ASSERT(iframe != nullptr);
  Thread::Current::ProcessPendingSignals(GeneralRegsSource::Iframe, iframe);
}

void arch_dump_exception_context(const arch_exception_context_t* context) {
  // If we don't have a frame, there's nothing more we can print.
  if (context->frame == nullptr) {
    printf("no frame to dump\n");
    return;
  }

  uint32_t ec = BITS_SHIFT(context->esr, 31, 26);
  uint32_t iss = BITS(context->esr, 24, 0);

  switch (ec) {
    case 0b100000: /* instruction abort from lower level */
    case 0b100001: /* instruction abort from same level */
      printf("instruction abort: PC at %#" PRIx64 ", address %#" PRIx64 " IFSC %#x %s\n",
             context->frame->elr, context->far, BITS(context->esr, 5, 0),
             BIT(ec, 0) ? "" : "user ");

      break;
    case 0b100100: /* data abort from lower level */
    case 0b100101: /* data abort from same level */
      printf("data abort: PC at %#" PRIx64 ", address %#" PRIx64 " %s%s\n", context->frame->elr,
             context->far, BIT(ec, 0) ? "" : "user ", BIT(iss, 6) ? "write" : "read");
  }

  dump_iframe(context->frame);

  // try to dump the user stack
  if (is_user_address(context->frame->usp)) {
    uint8_t buf[256];
    if (arch_copy_from_user(buf, (void*)context->frame->usp, sizeof(buf)) == ZX_OK) {
      printf("bottom of user stack at %#lx:\n", (vaddr_t)context->frame->usp);
      hexdump_ex(buf, sizeof(buf), context->frame->usp);
    }
  }
}

void arch_fill_in_exception_context(const arch_exception_context_t* arch_context,
                                    zx_exception_report_t* report) {
  zx_exception_context_t* zx_context = &report->context;

  zx_context->arch.u.arm_64.esr = arch_context->esr;

  // If there was a fatal page fault, fill in the address that caused the fault.
  if (ZX_EXCP_FATAL_PAGE_FAULT == report->header.type) {
    zx_context->arch.u.arm_64.far = arch_context->far;
  } else {
    zx_context->arch.u.arm_64.far = 0;
  }
}

zx_status_t arch_dispatch_user_policy_exception(void) {
  arch_exception_context_t context = {};
  return dispatch_user_exception(ZX_EXCP_POLICY_ERROR, &context);
}

bool arch_install_exception_context(Thread* thread, const arch_exception_context_t* context) {
  if (!context->frame) {
    // TODO(fxbug.dev/30521): Must be a synthetic exception as they don't (yet) provide the
    // registers.
    return false;
  }

  arch_set_suspended_general_regs(thread, GeneralRegsSource::Iframe, context->frame);
  thread->arch().debug_state.esr = context->esr;
  thread->arch().debug_state.far = context->far;
  return true;
}

void arch_remove_exception_context(Thread* thread) { arch_reset_suspended_general_regs(thread); }
