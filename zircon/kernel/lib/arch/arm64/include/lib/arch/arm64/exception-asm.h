// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_ARM64_INCLUDE_LIB_ARCH_ARM64_EXCEPTION_ASM_H_
#define ZIRCON_KERNEL_LIB_ARCH_ARM64_INCLUDE_LIB_ARCH_ARM64_EXCEPTION_ASM_H_

// This file provides macros primarily useful for the assembly code that
// defines an AArch64 exception vector table.  The implementation of the
// assembly macros is quite arcane.  But they are quite straightforward to use
// and it's pretty straightforward to read the disassembly and be convinced the
// code came out as intended.

#include <lib/arch/arm64/sysreg-asm.h>
#include <lib/arch/asm.h>

// The AArch64 exception vector is 2048 bytes naturally aligned in physical
// memory.  It holds 16 fixed-size pieces of code, in four groups of four.
// Each entry point fits 32 instructions in its 128 bytes.
#define ARCH_ARM64_VBAR_ALIGN_SHIFT 11
#define ARCH_ARM64_VBAR_SIZE (1 << ARCH_ARM64_VBAR_ALIGN_SHIFT)
#define ARCH_ARM64_VBAR_ENTRY_ALIGN_SHIFT 7
#define ARCH_ARM64_VBAR_ENTRY_SIZE (1 << ARCH_ARM64_VBAR_ENTRY_ALIGN_SHIFT)

// The table is divided into four groups of entry points based on the context
// in which the exception occurs: whether at the current exception level (EL)
// or a less-privileged (higher-numbered) EL; if from the current EL, whether
// the SPSel flag is set (using SP_ELx) or not (using SP_EL0); if from a lower
// EL, whether that lower EL is implementing AArch64 or AArch32.
#define ARCH_ARM64_VBAR_CURRENT_SP_EL0 0x000  // From current EL, using SP_EL0.
#define ARCH_ARM64_VBAR_CURRENT_SP_ELx 0x200  // From current EL, using SP_ELx.
#define ARCH_ARM64_VBAR_LOWER_A64 0x400       // From lower EL in AArch64 mode.
#define ARCH_ARM64_VBAR_LOWER_A32 0x600       // From lower EL in AArch32 mode.
#define ARCH_ARM64_VBAR_CONTEXT_MASK 0x600

// Within each group for a particular context, there are four entry points for
// the different types of exception.
#define ARCH_ARM64_VBAR_SYNC 0x000    // Synchronous (e.g. software)
#define ARCH_ARM64_VBAR_IRQ 0x080     // Asyncrhonous IRQ from peripheral
#define ARCH_ARM64_VBAR_FIQ 0x100     // Asyncrhonous FIQ from peripheral
#define ARCH_ARM64_VBAR_SERROR 0x180  // Asyncrhonous SError
#define ARCH_ARM64_VBAR_TYPE_MASK 0x180

#ifdef __ASSEMBLER__  // clang-format off

/// Perform `msr VBAR_ELx, ...` for the current EL and each lower EL.
///
/// Parameters
///
///  * scratch
///    - Required: Scratch register the macro will clobber (with CurrentEL).
///    - Type: register name
///
///  * value
///    - Required: Second operand for `msr` instruction.
///    - Type: expression
///
.macro msr_vbar_elx scratch, value:vararg
  mrs \scratch, CurrentEL
  cmp \scratch, #CURRENT_EL_EL_FIELD(1)
  beq 1f
  cmp \scratch, #CURRENT_EL_EL_FIELD(2)
  beq 2f
3:
  msr VBAR_EL3, \value
2:
  msr VBAR_EL2, \value
1:
  msr VBAR_EL1, \value
.endm

/// Define an exception vector table that extends until `.end_vbar_table`.
///
/// This starts a block of code for the exception vector entry points.
/// It's followed by one or more `.vbar_function` entries (see below).
/// The whole table is completed by the `.end_vbar_table` directive.
///
/// This defines the table as one contiguous object its own .text section
/// and defines a symbol for the whole table that can be used like a .object
/// symbol for the address to place in a VBAR_ELx register.  This symbol can
/// have local (by default) or global scope as with .object et al.
///
/// Each entry point can be defined using `.vbar_function`.  This is followed
/// by up to 32 instructions, and must be followed by `.end_vbar_function`.
/// All of the entry point symbols in the vector are always defined, whether
/// there is a `.vbar_function` defined for a given entry point or not.  These
/// functions get names based on the table name and a set of standard suffixes.
/// Each entry point symbol has the same scope as the overall table's symbol.
///
/// After `.vbar_table <name>` then each of the following can appear:
///   .vbar_function <name>_sync_current_sp_el0
///   .vbar_function <name>_irq_current_sp_el0
///   .vbar_function <name>_fiq_current_sp_el0
///   .vbar_function <name>_serror_current_sp_el0
///   .vbar_function <name>_sync_current_sp_elx
///   .vbar_function <name>_irq_current_sp_elx
///   .vbar_function <name>_fiq_current_sp_elx
///   .vbar_function <name>_serror_current_sp_elx
///   .vbar_function <name>_sync_lower_a64
///   .vbar_function <name>_irq_lower_a64
///   .vbar_function <name>_fiq_lower_a64
///   .vbar_function <name>_serror_lower_a64
///   .vbar_function <name>_sync_lower_a32
///   .vbar_function <name>_irq_lower_a32
///   .vbar_function <name>_fiq_lower_a32
///   .vbar_function <name>_serror_lower_a32
/// Any or all of them can be omitted, but any that are defined must be defined
/// in exactly the order shown.
///
/// All of those entry point symbols will be defined whether a .vbar_function
/// for each one appears or not.  Each omitted .vbar_function is defined as if
/// by invoking the macro whose name is passed as the `default` parameter to
/// .vbar_table, the default default being `.vbar_default_entry`.  This macro
/// should define code to run at the entry point whose name it's passed.  If
/// this is fewer than the available 32 instructions, the rest will be filled
/// with `brk #0` trap instructions (what __builtin_trap() in C++ does).  The
/// default `.vbar_default_entry` macro is just like an empty `.vbar_function`
/// so it generates nothing but 32 trap instructions.
///
/// Parameters
///
///  * name
///    - Required: Symbol name to define for the whole table.
///      This will be the address to store into VBAR_ELx.
///
///   * scope
///     - Optional: See `.label`.
///     - Default: `local`
///
///   * default
///     - Optional: This provides the name of an assembly macro (defined with
///       .macro) that will be invoked for each omitted entry point that does
///       not have its own `.vbar_function` invocation before this table ends
///       with `.end_vbar_table`.
///     - Default: `.vbar_default_entry`
///
.macro .vbar_table name, scope=local, default=.vbar_default_entry
  _.vbar_table \name, \scope, \default
.endm

// The default entry is just nothing but the standard trap fill.
.macro .vbar_default_entry name
.endm

// This sets up the CFI state to represent the vector entry point conditions.
// It should come after `.cfi_startproc simple`.  This is done implicitly at
// the beginning of `.vbar_function`.  The code that saves the interrupted
// registers or modifies SP should then use CFI directives to stay consistent.
.macro .vbar_function.cfi
  // The "caller" is actually interrupted register state.  This means the
  // "return address" will be treated as the precise PC of the "caller", rather
  // than as its return address that's one instruction after the call site.
  .cfi_signal_frame

  // The "return address" to find the caller's PC is via the rule for the PC
  // "register", not the CFI default of x30 (LR).
  .cfi_return_column 32  // This is the DWARG register number for the PC.

  // The interrupted PC value is found in ELR_ELx.
  .cfi_register 32, 33 // This is the DWARF register number for ELR_mode.

  // There are DWARF register numbers for each TPIDR_ELx.  We don't bother with
  // a CFI rule for those registers for two reasons: we can't statically
  // determine which ELx we're talking about here, and there are separate
  // TPIDR_ELx register numbers in DWARF for each one (unlike ELR_ELx where
  // there is just one DWARF register number that means ELR_ELx for the current
  // EL); and unwinders don't generally handle these registers anyway.  Since
  // TLS variables aren't used in kernel code, there are no variable locations
  // that would be resolved using that register value by a debugger.

  // We assume that SPSel.SP is always set: SP means SP_ELx for the current EL.
  // When coming from the current EL the interrupted SP is still the same SP.
  // The interrupted SP when coming from a lower EL is found in SP_ELx for the
  // lower EL.  There's no way to express that in CFI since there are no DWARF
  // register numbers for the SP_ELx registers.  So to be most accurate in that
  // case this would use .cfi_undefined instead for the from-lower-EL cases.
  // We don't bother with that since any real handler should start with saving
  // SP_ELx somewhere and immediately setting a new rule for the SP register.
  .cfi_same_value sp

  // Define the CFA as the SP on entry to the vector, that is SP_ELx for the
  // now-current EL.  When interrupted registers get saved on the stack, their
  // locations can easily be defined relative to this using a directive like
  // `.cfi_offset <register>, <byte offset>`.  Vector code that moves the SP
  // should use `.cfi_adjust_cfa_offset N` with a positive N to compensate for
  // subtracting N from SP, then a negative N to compensate for adding N back.
  .cfi_def_cfa sp, 0

  // All other registers still have their interrupted state.
  .cfi_same_value x0
  .cfi_same_value x1
  .cfi_same_value x2
  .cfi_same_value x3
  .cfi_same_value x4
  .cfi_same_value x5
  .cfi_same_value x6
  .cfi_same_value x7
  .cfi_same_value x8
  .cfi_same_value x9
  .cfi_same_value x10
  .cfi_same_value x11
  .cfi_same_value x12
  .cfi_same_value x13
  .cfi_same_value x14
  .cfi_same_value x15
  .cfi_same_value x16
  .cfi_same_value x17
  .cfi_same_value x18
  .cfi_same_value x19
  .cfi_same_value x20
  .cfi_same_value x21
  .cfi_same_value x22
  .cfi_same_value x23
  .cfi_same_value x24
  .cfi_same_value x25
  .cfi_same_value x26
  .cfi_same_value x27
  .cfi_same_value x28
  .cfi_same_value x29
  .cfi_same_value x30
  .cfi_same_value q0
  .cfi_same_value q1
  .cfi_same_value q2
  .cfi_same_value q3
  .cfi_same_value q4
  .cfi_same_value q5
  .cfi_same_value q6
  .cfi_same_value q7
  .cfi_same_value q8
  .cfi_same_value q9
  .cfi_same_value q10
  .cfi_same_value q11
  .cfi_same_value q12
  .cfi_same_value q13
  .cfi_same_value q14
  .cfi_same_value q15
  .cfi_same_value q16
  .cfi_same_value q17
  .cfi_same_value q18
  .cfi_same_value q19
  .cfi_same_value q20
  .cfi_same_value q21
  .cfi_same_value q22
  .cfi_same_value q23
  .cfi_same_value q24
  .cfi_same_value q25
  .cfi_same_value q26
  .cfi_same_value q27
  .cfi_same_value q28
  .cfi_same_value q29
  .cfi_same_value q30
  .cfi_same_value q31
.endm

// The rest of the macros are implementation details of .vbar_table.

.macro _.vbar_table.all name, macro:vararg
  _.vbar_table.all.where current_sp_el0, \name, \macro
  _.vbar_table.all.where current_sp_elx, \name, \macro
  _.vbar_table.all.where lower_a64, \name, \macro
  _.vbar_table.all.where lower_a32, \name, \macro
.endm

.L.vbar_table.where.current_sp_el0 = ARCH_ARM64_VBAR_CURRENT_SP_EL0
.L.vbar_table.where.current_sp_elx = ARCH_ARM64_VBAR_CURRENT_SP_ELx
.L.vbar_table.where.lower_a64 = ARCH_ARM64_VBAR_LOWER_A64
.L.vbar_table.where.lower_a32 = ARCH_ARM64_VBAR_LOWER_A32

.macro _.vbar_table.all.where where, name, macro:vararg
  _.vbar_table.all.what sync, \where, \name, \macro
  _.vbar_table.all.what irq, \where, \name, \macro
  _.vbar_table.all.what fiq, \where, \name, \macro
  _.vbar_table.all.what serror, \where, \name, \macro
.endm

.macro _.vbar_table.all.what what, where, name, macro:vararg
  \macro \name\()_\what\()_\where, (.L.vbar_table.where.\where + .L.vbar_table.what.\what)
.endm

.L.vbar_table.what.sync = ARCH_ARM64_VBAR_SYNC
.L.vbar_table.what.irq = ARCH_ARM64_VBAR_IRQ
.L.vbar_table.what.fiq = ARCH_ARM64_VBAR_FIQ
.L.vbar_table.what.serror = ARCH_ARM64_VBAR_SERROR

.macro _.vbar_table name, scope, default
  .pushsection .text.vbar_table.\name, "ax", %progbits
  .p2align ARCH_ARM64_VBAR_ALIGN_SHIFT
  .label \name, \scope, object

  _.vbar_table.all \name, _.vbar_table.label \name, \scope

  .macro .vbar_function vector
    _.vbar_function \name, \vector
  .endm

  .macro .end_vbar_table
    _.vbar_table.end \name
  .endm

  .purgem _.vbar_table.default
  .macro _.vbar_table.default vector
    \default \vector
  .endm
.endm

.macro _.vbar_table.default vector
.err "used too early"
.endm

.macro _.vbar_table.end name
  .purgem .vbar_function
  .purgem .end_vbar_table

  .if (. - \name) > ARCH_ARM64_VBAR_SIZE
    .err ".vbar_table \name too long!"
  .endif

   _.vbar_table.org \name, ARCH_ARM64_VBAR_SIZE
   _.vbar_table.align_entry \name

  .size \name, . - \name

  .if (. - \name) != ARCH_ARM64_VBAR_SIZE
    .err ".vbar_table \name contents too long!"
  .endif

  .popsection
.endm

.macro _.vbar_table.label table_name, scope, entry_name, offset
  .label \entry_name, \scope, function, (\table_name + \offset)
  .L.vbar_table.offset.\entry_name = \offset
.endm

.macro _.vbar_function table_name, entry_name
  _.vbar_table.org \table_name, .L.vbar_table.offset.\entry_name
  _.vbar_function.cfi
  .macro .end_vbar_function
    .purgem .end_vbar_function
    .cfi_endproc
    .size \entry_name, . - \entry_name
  .endm
.endm

// Pad any previous entry point with trap instructions.  This has no effect for
// the first one, as greater alignment was already done in _.vbar_table.  This
// is basically .p2alignl with a given fill pattern, but the assembler loses
// track of its arithmetic if you use the alignment directives and refuses to
// calculate the (. - table_start) expressions any more.
.macro _.vbar_table.align_entry table_name
  .rept ((ARCH_ARM64_VBAR_ENTRY_SIZE - ((. - \table_name) % ARCH_ARM64_VBAR_ENTRY_SIZE)) % ARCH_ARM64_VBAR_ENTRY_SIZE) / 4
    brk #0
  .endr
.endm

.macro _.vbar_table.org name, horizon
  .if (. - \name) > \horizon
    .err "too late for this entry point"
  .endif
  .macro _.vbar_table.org.current entry_name, entry_offset
    _.vbar_table.org.step \name, \horizon, \entry_name, \entry_offset
  .endm
  _.vbar_table.all \name, _.vbar_table.org.current
  .purgem _.vbar_table.org.current
.endm

// This runs once for each entry, in order.  The horizon is the offset we're
// trying to reach.  If the offset passed to this macro is > horizon, there
// is nothing to do in this step.
.macro _.vbar_table.org.step table_name, horizon, entry_name, offset
  .if \offset <= \horizon
    // Pad out the previous entry.
    _.vbar_table.align_entry \table_name
    // If . has reached the horizon, there is nothing more to do.
    .if (. - \table_name) < \horizon
      // This entry point has not been defined.  Give it the default.
      _.vbar_function.cfi
      _.vbar_table.default \entry_name
      .cfi_endproc
      .size \entry_name, . - \entry_name
    .endif
  .endif
.endm

// This defines initial CFI for the vector entry point.  This describes the
// interrupted state where all registers are still in their original locations.
.macro _.vbar_function.cfi
  // Start the FDE for this entry point.
  // .end_vbar_function completes the FDE with the matching .cfi_endproc.
  .cfi_startproc simple
  .vbar_function.cfi
.endm

#endif  // clang-format on

#endif  // ZIRCON_KERNEL_LIB_ARCH_ARM64_INCLUDE_LIB_ARCH_ARM64_EXCEPTION_ASM_H_
