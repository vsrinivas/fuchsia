// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2015 Intel Corporation
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/asm_macros.h>
#include <arch/code-patches/case-id.h>
#include <arch/regs.h>
#include <arch/x86/descriptor.h>
#include <arch/x86/mp.h>
#include <lib/arch/asm.h>
#include <lib/arch/x86/msr.h>
#include <lib/code-patching/asm.h>

// This is called 256 times in a row, with isr.current set to the iteration
// count starting at 0.  This is inside the definition for the ISR table,
// below (equivalent to being inside an .object ... .end_object pair,
// though those macros are not actually used because they don't allow
// nesting .function ... .end_function inside).  So the ambient section
// state is building up the table, and the macro defines a function using
// pushsection/popsection before adding its pointer to the table.
.macro isr.dispatch.define name
  // Make the whole set of macro-generated functions be cache-line aligned
  // collectively.
  .pushsection .text.isr.dispatch, "ax", %progbits
  .ifeq isr.current
    .balign 64
  .endif
  .function \name, cfi=custom, nosection=nosection
    // Set CFI for an interrupt frame.
    .cfi_signal_frame
    isr.current.has_error = isr.current == 8 || (isr.current >= 10 && isr.current <= 14) || isr.current == 17
    .cfi_def_cfa %rsp, (8 * (5 + isr.current.has_error))
    .cfi_offset %rip, -(5 * 8)
    // Mark each reg as having the same value as from the "calling" frame.
    // This is the default state for callee-saved registers, but for
    // completeness sake we do this for all of them.
    ALL_CFI_SAME_VALUE

    // Clear the AC flag to prevent ring 0 from performing data accesses to
    // ring 3 if SMAP is available.  If it was set, it will get restored by
    // iretd.  DO NOT REMOVE THIS CLAC, code in idt.c assumes it is here.
    // It MUST be the first instruction of this function.
    clac
   .if !isr.current.has_error
      // Fill in the error code not pushed by the hardware.
      push_value $0
   .endif
    // Fill in the interrupt number.
    push_value $isr.current
    JMP_AND_SPECULATION_POSTFENCE(interrupt_common)
  .end_function
  .popsection

  // Now we're back in the section building up the table.
  .quad \name
.endm

.macro isr.dispatch.define.next
  // The name is generated with an arbitrary number and is not actually
  // meaningful at all.  But there's no way to get isr.current into a
  // symbol name here.
  isr.dispatch.define isr.dispatch.\@.is.not.the.isr.number
  isr.current = isr.current + 1
.endm

// This defines the table with pointers to all the macro-generated functions.
// This does the same as .object, but .function can't be nested inside .object.
.pushsection .rodata.isr_table, "a", %progbits
.balign 8
.label _isr_table, global, object
  isr.current = 0
  .rept 256
    isr.dispatch.define.next
  .endr
.size _isr_table, . - _isr_table
.popsection

// This is the real function all the macro-generated functions tail-call into.
.function interrupt_common, global, align=64, cfi=custom
  // Set CFI for an interrupt frame, with all the words pushed by the hardware
  // and the macro-generated functions together.
  .cfi_signal_frame
  .cfi_def_cfa %rsp, 7 * 8
  .cfi_offset %rip, -(5 * 8)
  // Mark each reg as having the same value as from the "calling" frame.
  // This is the default state for callee-saved registers, but for completeness
  // sake we do this for all of them.
  ALL_CFI_SAME_VALUE

  // Clear the direction flag.  Without this, uses of string
  // instructions, e.g. REP MOVS in memcpy() or inlined by the compiler,
  // can go wrong and copy in the wrong direction, since this code may
  // assume that the direction flag is unset.
  cld

  // Later on we're going to calculate the percpu pointer using dead reckoning
  // from %rsp so keep track of what all has been pushed onto the stack.
  //
  // At this point the stack contains 7 qwords.  5 or 6 pushed by the interrupt
  // and 2 or 1 pushed by the stub.

  // Save general purpose registers.
  push_reg %r15
  push_reg %r14
  push_reg %r13
  push_reg %r12
  push_reg %r11
  push_reg %r10
  push_reg %r9
  push_reg %r8
  push_reg %rax
  push_reg %rcx
  push_reg %rdx
  push_reg %rbx
  push_reg %rbp
  push_reg %rsi
  push_reg %rdi
  // Zero general-purpose registers to constrain speculative execution with
  // potentially-hostile register values; the values may have come from user
  // mode and could be consumed by in-kernel speculative execution.
  // Zeroing of ebp is delayed till we check precisely if this was a user fault
  // so that kernel faults can have correct stack frames for backtraces.
  xorl %eax, %eax
  xorl %ebx, %ebx
  xorl %ecx, %ecx
  xorl %edx, %edx
  xorl %esi, %esi
  xorl %edi, %edi
  xorq %r8, %r8
  xorq %r9, %r9
  xorq %r10, %r10
  xorq %r11, %r11
  xorq %r12, %r12
  xorq %r13, %r13
  xorq %r14, %r14
  xorq %r15, %r15

  // At this point the stack contains a complete iframe_t.

  movq %rsp, %rdi     // Pass the iframe in %rdi.

  // There are two main paths through this function.  One path is for NMIs.  The
  // other is for all other interrupts (non_nmi).  Both share a common return
  // path.
  //
  // TODO(maniscalco): Refactor this function to eliminate the compare and branch
  // without duplicating logic that's common to the NMI and non-NMI paths.

  // Was this an NMI?
  cmpq $2, X86_IFRAME_OFFSET_VECTOR(%rsp)
  je .Lnmi

  // Check to see if we came from user space by testing the CPL in the
  // %cs selector that was saved on the stack automatically.  Check for != 0.
  testb $3, X86_IFRAME_OFFSET_CS(%rsp)
  jz 1f
  // Perform the last zero from the previous block now that we know this is a
  // user fault and we do not need the stack frame.
  xorl %ebp, %ebp
  // Swap %gs.base to kernel space.
  swapgs
1:
  // Mitigates the swapgs bug. See <arch/code-patches/case-id.h>.
  .code_patching.start CASE_ID_SWAPGS_MITIGATION
  lfence
  .code_patching.end

  call x86_exception_handler

  // A label to assist gdb's backtracing through kernel exceptions.
  // When gdb sees this as the return address it knows it can fetch
  // iframe_t from $rsp. See scripts/zircon.elf-gdb.py.
.label interrupt_common_iframe_set_up_for_debugger

  // Label exists only so a test can verify the code patching result.
.label interrupt_non_nmi_maybe_mds_buff_overwrite, global
  // Mitigates MDS/TAA bugs. See <arch/code-patches/case-id.h>
  .code_patching.start CASE_ID_MDS_TAA_MITIGATION
  call mds_buff_overwrite
  .code_patching.end

  // Check if we're returning to user space as per before.
  testb $3, X86_IFRAME_OFFSET_CS(%rsp)
  jz    1f
  // Swap %gs.base back to user space.
  swapgs
1:
  // Mitigates the swapgs bug. See <arch/code-patches/case-id.h>.
  .code_patching.start CASE_ID_SWAPGS_MITIGATION
  lfence
  .code_patching.end

.Lcommon_return:

  // Restore general purpose registers.
  pop_reg %rdi
  pop_reg %rsi
  pop_reg %rbp
  pop_reg %rbx
  pop_reg %rdx
  pop_reg %rcx
  pop_reg %rax
  pop_reg %r8
  pop_reg %r9
  pop_reg %r10
  pop_reg %r11
  pop_reg %r12
  pop_reg %r13
  pop_reg %r14
  pop_reg %r15

  // Drop vector number and error code.
  add_to_sp 16

  iretq

.Lnmi:
  // We took an NMI.  The NMI may have interrupted another interrupt handler
  // before that handler was able to setup the kernel's %gs.base.  We can't
  // be sure of the state of %gs.base so we'll have to recover it.  Start by
  // saving the current value in a callee-saved register so we can later restore
  // it.

  // Save it in %rbx.
  //
  // TODO(maniscalco): If/when NMIs become performance critical (e.g. used for
  // profiling) consider using rdgsbase/wrgsbase when available instead of the
  // MSR.
  rdmsr64 MSR_IA32_GS_BASE
  mov %rax, %rbx

  // Compute the %rsp-relative offset of the stack base.
  .Lstack_base_from_rsp = INTERRUPT_STACK_SIZE - X86_IFRAME_SIZE
  // The NMI stack is embedded in the percpu struct.
  .Lpercpu_from_rsp = .Lstack_base_from_rsp + PERCPU_INTERRUPT_STACKS_NMI_OFFSET
  mov %rsp, %rax
  sub $.Lpercpu_from_rsp, %rax

  // Set %gs.base to point at the percpu struct.
  wrmsr64 MSR_IA32_GS_BASE

  call x86_nmi_handler

  // Label exists only so a test can verify the code patching result.
.label interrupt_nmi_maybe_mds_buff_overwrite, global
  // Mitigates MDS/TAA bugs. See <arch/code-patches/case-id.h>
  .code_patching.start CASE_ID_MDS_TAA_MITIGATION
  call mds_buff_overwrite
  .code_patching.end

  // Restore the saved %gs.base.
  mov %rbx, %rax
  wrmsr64 MSR_IA32_GS_BASE

  JMP_AND_SPECULATION_POSTFENCE(.Lcommon_return)

.end_function

// Call external interrupt handler manually without actually issuing interrupt.
//
// For external interrupts CPU doesn't store error code on stack so we use
// 0. We additionally use CODE_64_SELECTOR as CS, 0 as SS, RFLAGS value and
// current stack.
.function x86_call_external_interrupt_handler, global
  // Save current RFLAGS value.
  pushfq
  .cfi_adjust_cfa_offset 8
  pop_value %r10

  // Save current RSP value.
  movq %rsp, %r11

  // Calculate exit address.
  leaq .Lexit(%rip), %rax

  // Prepare interrupt stack frame in the form interrupt_common expects to see.
  sub_from_sp 0x38
  movq %rdi, 0x00(%rsp)              // %rdi holds vector number
  movq $0, 0x08(%rsp)                // error code
  movq %rax, 0x10(%rsp)              // RIP (return address)
  movq $CODE_64_SELECTOR, 0x18(%rsp) // CS
  movq %r10, 0x20(%rsp)              // RFLAGS
  movq %r11, 0x28(%rsp)              // RSP
  movq $0, 0x30(%rsp)                // SS

  // We can actually avoid this jump if we put this code above
  // interrupt_common and just fall through, but benefits of doing this are
  // not obvious so for now for the sake of clarity keep this jump.
  JMP_AND_SPECULATION_POSTFENCE(interrupt_common)

.Lexit:
  RET_AND_SPECULATION_POSTFENCE
.end_function
