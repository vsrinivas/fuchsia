// Copyright 2020 The Fuchsia Authors
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
#include <arch/regs.h>
#include <zircon/types.h>
#include <syscalls/syscalls.h>

#include <arch/arch_ops.h>
#include <arch/exception.h>
#include <arch/thread.h>
#include <arch/user_copy.h>
#include <kernel/interrupt.h>
#include <kernel/thread.h>
#include <pretty/hexdump.h>
#include <vm/fault.h>
#include <vm/vm.h>

#define LOCAL_TRACE 0

static zx_status_t try_dispatch_user_data_fault_exception(zx_excp_type_t type,
                                                          iframe_t* iframe) {
  arch_exception_context_t context = {};
  DEBUG_ASSERT(iframe != nullptr);
  context.frame = iframe;

  arch_enable_ints();
  zx_status_t status = dispatch_user_exception(type, &context);
  arch_disable_ints();
  return status;
}

void arch_iframe_process_pending_signals(iframe_t* iframe) {
}

void arch_dump_exception_context(const arch_exception_context_t* context) {
}

void arch_fill_in_exception_context(const arch_exception_context_t* arch_context,
                                    zx_exception_report_t* report) {
}

zx_status_t arch_dispatch_user_policy_exception(uint32_t policy_exception_code) {
  return ZX_OK;
}

bool arch_install_exception_context(Thread* thread, const arch_exception_context_t* context) {
  return true;
}

void arch_remove_exception_context(Thread* thread) { }

static inline void riscv64_restore_percpu_pointer() {
  riscv64_set_percpu(arch_get_current_thread()->arch().current_percpu_ptr);
}

static const char *cause_to_string(long cause) {
  if (cause < 0) {
    switch (cause & LONG_MAX) {
      case RISCV64_INTERRUPT_SSWI:
	return "Software interrupt";
        break;
      case RISCV64_INTERRUPT_STIM:
	return "Timer interrupt";
      case RISCV64_INTERRUPT_SEXT:
	return "External interrupt";
    }
  } else {
    switch (cause) {
      case RISCV64_EXCEPTION_IADDR_MISALIGN:
        return "Instruction address misaligned";
      case RISCV64_EXCEPTION_IACCESS_FAULT:
        return "Instruction access fault";
      case RISCV64_EXCEPTION_ILLEGAL_INS:
        return "Illegal instruction";
      case RISCV64_EXCEPTION_BREAKPOINT:
        return "Breakpoint";
      case RISCV64_EXCEPTION_LOAD_ADDR_MISALIGN:
        return "Load address misaligned";
      case RISCV64_EXCEPTION_LOAD_ACCESS_FAULT:
        return "Load access fault";
      case RISCV64_EXCEPTION_STORE_ADDR_MISALIGN:
        return "Store/AMO address misaligned";
      case RISCV64_EXCEPTION_STORE_ACCESS_FAULT:
        return "Store/AMO access fault";
      case RISCV64_EXCEPTION_ENV_CALL_U_MODE:
        return "Environment call from U-mode";
      case RISCV64_EXCEPTION_ENV_CALL_S_MODE:
        return "Environment call from S-mode";
      case RISCV64_EXCEPTION_ENV_CALL_M_MODE:
        return "Environment call from M-mode";
      case RISCV64_EXCEPTION_INS_PAGE_FAULT:
        return "Instruction page fault";
      case RISCV64_EXCEPTION_LOAD_PAGE_FAULT:
        return "Load page fault";
      case RISCV64_EXCEPTION_STORE_PAGE_FAULT:
        return "Store/AMO page fault";
    }
  }
  return "Unknown";
}

__NO_RETURN __NO_INLINE
static void fatal_exception(long cause, struct iframe_t *frame) {
  if (cause < 0) {
    panic("unhandled interrupt cause %#lx, epc %#lx, tval %#lx cpu %u\n", cause,
	  frame->epc, riscv64_csr_read(RISCV64_CSR_STVAL), arch_curr_cpu_num());
  } else {
    panic("unhandled exception cause %#lx (%s), epc %#lx, tval %#lx, cpu %u\n",
	  cause, cause_to_string(cause), frame->epc,
	  riscv64_csr_read(RISCV64_CSR_STVAL), arch_curr_cpu_num());
  }
}

static void riscv64_page_fault_handler(long cause, struct iframe_t *frame) {
  vaddr_t tval = riscv64_csr_read(RISCV64_CSR_STVAL);
  uint pf_flags = VMM_PF_FLAG_NOT_PRESENT;
  pf_flags |= cause == RISCV64_EXCEPTION_STORE_PAGE_FAULT ? VMM_PF_FLAG_WRITE : 0;
  pf_flags |= cause == RISCV64_EXCEPTION_INS_PAGE_FAULT ? VMM_PF_FLAG_INSTRUCTION : 0;
  pf_flags |= !(frame->status & RISCV64_CSR_SSTATUS_PP) ? VMM_PF_FLAG_USER : 0;

  zx_status_t pf_status = vmm_page_fault_handler(tval, pf_flags);

  if (pf_status != ZX_OK) {
    uint64_t dfr = Thread::Current::Get()->arch().data_fault_resume;
    if (unlikely(dfr)) {
      frame->epc = dfr;
      frame->a1 = tval;
      frame->a2 = pf_flags;
      return;
    }

    // If this is from user space, let the user exception handler get a shot at it.
    if (pf_flags & VMM_PF_FLAG_USER) {
      if (try_dispatch_user_data_fault_exception(ZX_EXCP_FATAL_PAGE_FAULT, frame) == ZX_OK) {
        return;
      }
    } else {
      panic("Kernel page fault");
    }
  }
}

static bool riscv64_is_floating_point_instruction(long instruction) {
  // All floating point instructions have the following bit patterns in the
  // 7 lsb "opcode" portion of the instruction:
  //
  //    0000111 LOAD-FP (width determined by bits 12-14)
  //    0100111 STORE-FP (width determined by bits 12-14)
  //    1010011 OP-FP (specific FP instruction determined by other bits)
  //    1000011 FMADD
  //    1000111 FMSUB
  //    1001011 FNMSUB
  //    1001111 FNMADD

  instruction &= 0b1111111;
  return (instruction == 0b0000111) ||  // LOAD-FP
         (instruction == 0b0100111) ||  // STORE-FP
         (instruction == 0b1010011) ||  // OP-FP
         (instruction == 0b1000011) ||  // FMADD
         (instruction == 0b1000111) ||  // FMSUB
         (instruction == 0b1001011) ||  // FNMSUB
         (instruction == 0b1001111);    // FNMADD
}

static void riscv64_illegal_instruction_handler(long cause, struct iframe_t *frame) {
  long instruction = riscv64_csr_read(RISCV64_CSR_STVAL);
  if (riscv64_is_floating_point_instruction(instruction)) {
    // A floating point instruction was used but floating point support is not
    // enabled.  Enable it now.
    if ((frame->status & RISCV64_CSR_SSTATUS_FS) != RISCV64_CSR_SSTATUS_FS_OFF) {
      panic("FP already enabled %#lx, epc %#lx, inst %#lx, cpu %u", cause,
          frame->epc, instruction, arch_curr_cpu_num());
    }
    frame->status |= RISCV64_CSR_SSTATUS_FS_INITIAL;
  } else if (!(frame->status & RISCV64_CSR_SSTATUS_PP)) {
    // An illegal instruction happened in a user thread.  Handle the execption.
    // This will kill the process.
    try_dispatch_user_data_fault_exception(ZX_EXCP_UNDEFINED_INSTRUCTION,
        frame);
  } else {
    // An illegal instruction happened in a kernel thread.  That's bad, panic.
    panic("Exception in a kernel thread %#lx, epc %#lx, inst %#lx, cpu %u",
        cause, frame->epc, instruction, arch_curr_cpu_num());
  }
}

extern "C" syscall_result riscv64_syscall_dispatcher(struct iframe_t *frame);

static void riscv64_syscall_handler(struct iframe_t *frame) {
  frame->epc = frame->epc + 0x4; // Skip the ecall instruction

  struct syscall_result ret = riscv64_syscall_dispatcher(frame);
  frame->a0 = ret.status;
  if (ret.is_signaled) {
    Thread::Current::ProcessPendingSignals(GeneralRegsSource::Iframe, frame);
  }
}

extern "C" void riscv64_exception_handler(long cause, struct iframe_t *frame) {
  riscv64_restore_percpu_pointer();

  LTRACEF("hart %u cause %s epc %#lx status %#lx\n",
          arch_curr_cpu_num(), cause_to_string(cause), frame->epc, frame->status);

  // top bit of the cause register determines if it's an interrupt or not
  if (cause < 0) {
    int_handler_saved_state_t state;
    int_handler_start(&state);

    switch (cause & LONG_MAX) {
      case RISCV64_INTERRUPT_SSWI: // software interrupt
        riscv64_software_exception();
        break;
      case RISCV64_INTERRUPT_STIM: // timer interrupt
        riscv64_timer_exception();
        break;
      case RISCV64_INTERRUPT_SEXT: // external interrupt
        platform_irq(frame);
        break;
      default:
        fatal_exception(cause, frame);
    }

    bool do_preempt = int_handler_finish(&state);
    if (do_preempt) {
      Thread::Current::Preempt();
    }
  } else {
    // all synchronous traps go here
    switch (cause) {
      case RISCV64_EXCEPTION_INS_PAGE_FAULT:
      case RISCV64_EXCEPTION_LOAD_PAGE_FAULT:
      case RISCV64_EXCEPTION_STORE_PAGE_FAULT:
        riscv64_page_fault_handler(cause, frame);
        break;
      case RISCV64_EXCEPTION_ILLEGAL_INS:
        riscv64_illegal_instruction_handler(cause, frame);
        break;
      case RISCV64_EXCEPTION_ENV_CALL_U_MODE:
        riscv64_syscall_handler(frame);
        break;
      default:
        fatal_exception(cause, frame);
    }
  }
}
