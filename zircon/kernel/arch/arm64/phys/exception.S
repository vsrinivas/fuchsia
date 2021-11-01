// Copyright 2021 The Fuchsia Authors>
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/arm64/exception-asm.h>
#include <lib/arch/arm64/sysreg-asm.h>
#include <lib/arch/asm.h>
#include <phys/exception.h>
#include <phys/stack.h>

#include "regs.h"

// The given register points to a zx_thread_state_general_regs_t at the CFA.
// Store a pair of registers there and update the CFI.  The first two arguments
// are adjacent raw register numbers (e.g. 0, 1 for x0 and x1).
.macro stp_cfa r0, r1, cfa=sp
  stp x\r0, x\r1, [\cfa, #REGS_X(\r0)]
  .cfi_offset x\r0, REGS_X(\r0)
  .cfi_offset x\r1, REGS_X(\r1)
.endm

// Reload a pair of registers saved by stp_cfa.
.macro ldp_cfa r0, r1, cfa=sp
  ldp x\r0, x\r1, [\cfa, #REGS_X(\r0)]
  .cfi_same_value x\r0
  .cfi_same_value x\r1
.endm

// Invoke `\macro 2, 3`, ..., `\macro 28, 29`.
.macro .for_each_pair macro:vararg
  \macro 2, 3
  \macro 4, 5
  \macro 6, 7
  \macro 8, 9
  \macro 10, 11
  \macro 12, 13
  \macro 14, 15
  \macro 16, 17
  \macro 18, 19
  \macro 20, 21
  \macro 22, 23
  \macro 24, 25
  \macro 26, 27
  \macro 28, 29
.endm

// This supplies the code (as if inside `.vbar_function`) that the phys
// exception table will use by default.
.macro phys_exception_entry name
  // On entry the CFI state thinks the CFA is the SP.  But the SP is the
  // interrupted SP, or SP_ELx if coming from lower EL.  On the assumption
  // we're coming from the current EL so the interrupted SP is still in SP,
  // we don't want to use that SP.  Instead we'll move to a dedicated stack
  // used only for exceptions.  (There's just one, so nested any exceptions
  // will just start over reusing it from the top.)  So the SP we're about
  // to switch to is what we'd really like to call the CFA.  That value is
  // not in any register.
#define EXC_ENTRY_CFA (phys_exception_stack + BOOT_STACK_SIZE - REGS_SIZE)
#if BOOT_STACK_SIZE % BOOT_STACK_ALIGN != 0
#error "BOOT_STACK_SIZE not aligned"
#endif
#if REGS_SIZE % BOOT_STACK_ALIGN != 0
#error "REGS_SIZE not aligned"
#endif

  // TODO(mcgrathr): DW_CFA_def_cfa_expression with DW_OP_addr could just point
  // to its fixed address.  But the assembler doesn't know how to generate that
  // and .cfi_escape only takes byte values and can't generate a relocation for
  // the address.  The GNU assembler has a .cfi_val_encoded_addr directive that
  // generates a DW_OP_addr expression just like that for you, but that is only
  // for DW_CFA_val_expression (a register rule, not the CFA) and LLVM doesn't
  // implement that anyway.  So the CFA is inaccurate at the first instruction.
//.cfi_def_cfa_encoded_addr EXC_ENTRY_CFA

  // Since the percpu_ptr register is reserved in all kernel code, and not
  // actually used at all by phys code, we can clobber it here to avoid
  // touching any other register whose value might be interesting to dump,
  // while also not relying on the incoming SP value.  When the exception is
  // from the current EL, the incoming SP might be close to overflow or
  // entirely bogus but it's definitely interesting state to dump.
  adr_global percpu_ptr, EXC_ENTRY_CFA

  // The interrupted percpu_ptr has been clobbered and cannot be recovered.
  .cfi_undefined percpu_ptr

  // Temporarily use the percpu_ptr as the CFA.  It will become the new SP.
  .cfi_def_cfa percpu_ptr, 0

  // First save the incoming x30 and sp so we have two registers to work with.
  str x30, [percpu_ptr, #REGS_X(30)]
  mov x30, sp
  str x30, [percpu_ptr, #REGS_SP]

  // Now we can reset the SP to the reserved exception stack, just below where
  // the registers will be saved.  This is the SP value that's the CFA.
  mov sp, percpu_ptr
  .cfi_def_cfa_register sp

  // Update CFI for the registers now visible relative to the CFA (the SP).
  .cfi_offset x30, REGS_X(30)
  .cfi_offset sp, REGS_SP

  // Spill the first pair of registers inline so we get some more to use here.
  stp_cfa 0, 1

  // The first argument register gets the vector offset as a handy identifier.
  mov x0, #(\name - phys_exception)

  // The second argument register gets the name of this entry point.
  adr_global x1, 1f
  .pushsection .rodata.str1.1, "aMS?", %progbits, 1
  1: .string "\name"
  .popsection

  // Now we can do the rest out of line.
  b phys_exception_trampoline
.endm

// Define the vector table for phys.  There are no .vbar_function definitions
// here so they all get the default defined above.
.vbar_table phys_exception, global, phys_exception_entry
.end_vbar_table

// This is reached by each phys_exception_entry, with x0, x1, x30, sp already
// saved in the zx_thread_state_general_regs_t that sp now points to.
.function phys_exception_trampoline, cfi=custom

  // This is discontiguous code so it gets its own fresh CFI state (FDE).
  // Reset the CFI state to just as it was at the end of phys_exception_entry.
  .vbar_function.cfi

  // The SP that points to the saved registers is the CFA.  This is still the
  // same "frame" as the phys_exception_entry code fragment, though it's at a
  // discontiguous PC location.
  .cfi_def_cfa sp, 0

  // These registers were already saved in the phys_exception_entry code above.
  .cfi_offset x0, REGS_X(0)
  .cfi_offset x1, REGS_X(1)
  .cfi_offset x30, REGS_X(30)
  .cfi_offset sp, REGS_SP

  // Now save the remaining registers in the zx_thread_state_general_regs_t at
  // sp and update CFI to find them relative to the CFA.
  .for_each_pair stp_cfa

  // Normal registers are all free as scratch right now, except x0 and x1.
  // Use a few to collect the special registers we need to record.
  // Which ones they are depends on the current EL.
  mrs x2, CurrentEL

  // First get the SPSR_ELx for the current EL.
  cmp x2, #CURRENT_EL_EL_FIELD(1)
  beq 1f
  cmp x2, #CURRENT_EL_EL_FIELD(2)
  beq 2f
3:mrs x3, SPSR_EL3
  b 0f
2:mrs x3, SPSR_EL2
  b 0f
1:mrs x3, SPSR_EL1
0:

  // These bits in SPSR_ELx indicate what EL the exception came from.
  // Now fetch the SPSR_ELx for that EL.
  and x4, x3, #CURRENT_EL_EL

.macro exc_regs_for_el el
  mrs x5, SPSR_EL\el
  mrs x6, TPIDR_EL\el
  mrs x7, ELR_EL\el
  mrs x8, ESR_EL\el
  mrs x9, FAR_EL\el
.endm

  cmp x4, #CURRENT_EL_EL_FIELD(1)
  beq 1f
  cmp x4, #CURRENT_EL_EL_FIELD(2)
  beq 2f
3:exc_regs_for_el 3
  b 0f
2:exc_regs_for_el 2
  b 0f
1:exc_regs_for_el 1
0:

#if REGS_CPSR + 8 != REGS_TPIDR
#error zx_thread_state_general_regs_t expected cpsr before tpidr
#endif
  stp x5, x6, [sp, REGS_CPSR]
  str x7, [sp, REGS_PC]

#if REGS_ESR + 8 != REGS_FAR
#error zx_exception_context_t expected arm.esr before arm.far
#endif
  stp x8, x9, [sp, REGS_ESR]
  stp xzr, xzr, [sp, REGS_XZR]

#if __has_feature(shadow_call_Stack)
  // Reset the shadow call stack to the one reserved for phys exceptions.
  adr_global shadow_call_sp, phys_exception_shadow_call_stack  // Grows up.
#endif

  // Reset the thread pointer, though nothing should have changed it.
  adr_global x8, boot_thread_pointer
  msr TPIDR_EL1, x8

#if __has_feature(safe_stack)
  // Reset the unsafe stack pointer in the thread area.
  adr_global x9, phys_exception_unsafe_stack + BOOT_STACK_SIZE  // Grows down.
  str x9, [x8, #ZX_TLS_UNSAFE_SP_OFFSET]
#endif

  // We now have complete CFI representing all the general registers so a
  // debugger can unwind through this frame and back to the interrupted code.
  // A normal call into C++ code will permit unwinding back into this frame.
  // All the ABI stacks have been set up safely so normal C++ code can run.

  // The first two argument registers were loaded in phys_exception_entry.
  // Load the third argument register with the zx_thread_state_general_regs_t*.
  mov x2, sp

  // Make a first frame pointer record on the exception stack that links to
  // the interrupted state's FP and PC.
  stp x29, x7, [sp, #-16]!
  .cfi_adjust_cfa_offset 16
  mov x29, sp

  // Show the interrupted PC as the earliest caller on the shadow call stack.
  // A backtrace via the exception shadow call stack won't go past there like
  // the frame pointer might, but it will at least clearly bottom out at the
  // exception PC.
#if __has_feature(shadow_call_stack)
  str x7, [shadow_call_sp], #8
#endif

  // uint64_t PhysException(uint64_t vector_offset, const char* vector_name,
  //                        zx_thread_state_general_regs_t* regs);
  // x0 = vector_offset
  // x1 = vector_name
  // x2 = regs
  bl PhysException

  // If it returns the magic number, we can resume.
  movlit x1, PHYS_EXCEPTION_RESUME
  cmp x0, x1
  beq .Lresume_from_exception

 // Otherwise it really shouldn't have returned.  Trigger an exception reentry.
 // What else?
.label PhysExceptionHandler_returned_BUG, global
  brk #0
  b PhysExceptionHandler_returned_BUG

.Lresume_from_exception:
  // The handler code should have restored the SP it got on entry.
  cmp sp, x29
  bne PhysExceptionHandler_returned_BUG

  // Pop the frame pointer link pair pushed above.
  add sp, sp, 16
  .cfi_adjust_cfa_offset -16

  // Reload the interrupted x0..x29 registers.
  ldp_cfa 0, 1
  .for_each_pair ldp_cfa

  // Reload the interrupted x30 and sp registers.
  ldp x30, percpu_ptr, [sp, #REGS_X(30)]
  .cfi_same_value x30

  // The SP isn't allowed in a load instruction, so stage it through the
  // reserved percpu_ptr.  This one register will always be reset (to match
  // the interrupted SP).
  mov sp, percpu_ptr
  .cfi_same_value sp
  .cfi_def_cfa_register percpu_ptr

  // Return to the interrupted PC.  The C++ code is responsible for updating
  // ELR_ELx.  We don't reload it from [sp, #REGS_PC] here.  Likewise if
  // returning to a lower EL, the C++ code must update SPSR_ELx first.
  eret
.end_function  // phys_exception_save
