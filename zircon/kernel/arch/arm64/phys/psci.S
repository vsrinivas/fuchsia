// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/arm64/exception-asm.h>
#include <lib/arch/asm.h>

// Do the magic instruction.
.macro do_psci_call
  ldr_global x16, gArmPsciUseHvc
  cbnz x16, 0f
  smc #0
  b 1f
0:hvc #0
1:
.endm

// extern "C" uint64_t ArmPsciCall(...);
//
// This is called with argument registers x0, x1, x2, and x3 set.
// It returns only a single uint64_t value in x0.
.function ArmPsciCall, global
  .prologue.shadow_call_sp
  .prologue.fp

  do_psci_call

  .epilogue.fp
  .epilogue.shadow_call_sp
  ret
.end_function

// extern "C" [[noreturn]] void ArmPsciReset();
//
// This is called with no arguments and does not touch the stack.
// It reads the memory necessary to load up the PSCI call arguments
// but that is all.  It can never return.

.function ArmPsciReset, global

  ldr_global x16, gArmPsciDisabled
  cbnz x16, .Lfail

  // Reset the exception vector base address registers in hopes that this will
  // cause any exceptions to lead to a hardware reset rather than a loop back
  // through the exception.S handler code.  There's not really much hope
  // without FEAT_DOUBLEFAULT, but the failure mode might be less confusing.
  msr_vbar_elx x16, xzr

  // Copy in all the register values that might be SMCCC arguments.
  adr_global x16, gArmPsciResetRegisters
  ldp x0, x1, [x16, #0]
  ldp x2, x3, [x16, #16]

  // Do the call.
  do_psci_call

  // If it came back there's not much we can do.  Try to crash somehow.
.Lfail:
  brk #0
  b ArmPsciReset

.end_function
