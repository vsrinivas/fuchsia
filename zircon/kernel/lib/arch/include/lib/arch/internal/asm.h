// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_INTERNAL_ASM_H_
#define ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_INTERNAL_ASM_H_

// These macros are internal implementation details of <lib/arch/asm.h>.

#ifdef __ASSEMBLER__  // clang-format off

// This macro is purged and redefined to indicate whether the assembly
// is currently between an entity-defining macro and its matching `.end_*`.
.macro _.entity.assert
  // Initial state: no entity being defined.
.endm

// Subroutine of _.entity.  Redefine the `.end_*` macros as errors.
.macro _.entity.reset purge=, text:vararg
  .ifnb \purge
    .purgem .end_function
    .purgem .end_object
  .endif
  .macro .end_function
    .error "unmatched .end_function directive \text"
  .endm  // .end_function
  .macro .end_object
    .error "unmatched .end_object directive \text"
  .endm  // .end_object
.endm  // _.entity.reset

// Subroutine of entity-defining macros below.  This resets the `.end_*`
// machinery so that it does `\epilogue` and then does `.size` for `\name`,
// via `_.entity.end`.  The \name, \scope, and \nosection arguments are
// passed through from caller and validated here; the rest are provided by
// each entity-defining macro.
.macro _.entity name, scope, align, nosection, entity, type, epilogue
  // First make sure that entity and `.end_*` pairs are matched (no nesting).
  _.entity.assert
  .purgem _.entity.assert
  .macro _.entity.assert
    .error "missing `.end_\entity` for `.\entity \name`"
  .endm

  // Clear old `.end_*` macros and make the one other than `.end_\entity`
  // an error until this entity is finished.
  _.entity.reset purge, inside definition of `.\entity \name`

  // Now redefine the `.end_\entity` macro to pair with this entity.
  // This definition captures the parameters to pass on to `_.entity.end`.
  .purgem .end_\entity
  .macro .end_\entity
    _.entity.end \entity, \name, \nosection, \epilogue
  .endm

  // Unless given the `nosection` flag argument, enter a per-entity section.
  .ifb \nosection
    // The specific section name and details depend on the entity type.
    _.entity.pushsection.\type \name
  .else
    .ifnc \nosection, nosection
      .error "final argument to \entity directive must be exactly `nosection`"
    .endif
  .endif

  // Align within the section.
  .ifnb \align
    .balign \align
  .endif

  // Finally, define the actual label.  The entity's own prologue comes next.
  .label \name, \scope, \entity
.endm  // _.entity

// Subroutines of _.entity selected by the \type argument.

.macro _.entity.pushsection.function name
  .pushsection .text.\name, "ax", %progbits
.endm

.macro _.entity.pushsection.bss name
  .pushsection .bss.\name, "aw", %nobits
.endm

.macro _.entity.pushsection.data name
  .pushsection .data.\name, "aw", %progbits
.endm

.macro _.entity.pushsection.relro name
  .pushsection .data.rel.ro.\name, "aw", %progbits
.endm

.macro _.entity.pushsection.rodata name
  .pushsection .rodata.\name, "a", %progbits
.endm

// Subroutine of `.end_\entity` macros defined by `_.entity`, above.
.macro _.entity.end entity, name, nosection, epilogue
  // First, reset the assertion machinery.
  .purgem _.entity.assert
  .macro _.entity.assert
  .endm

  // Now redefine `.end_*` so it's an error with no matching entity.
  _.entity.reset purge

  // Do the epilogue for the entity, e.g. .cfi_endproc.
  \epilogue

  // Set the ELF symbol's `st_size`.
  .size \name, . - \name

 // Leave the per-entity section, if any.
  .ifb \nosection
    .popsection
  .endif
.endm  // _.entity.end

// Subroutines of .function, start/end pairs for each `cfi` mode.

.macro _.function.start.abi
  .cfi_startproc
.endm
.macro _.function.end.abi
  .cfi_endproc
.endm

.macro _.function.start.custom
  .cfi_startproc simple
.endm
.macro _.function.end.custom
  .cfi_endproc
.endm

.macro _.function.start.none
.endm
.macro _.function.end.none
.endm

// Catch stray `.end_*` before any entity-defining macros.
_.entity.reset

#endif  // clang-format on

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_INTERNAL_ASM_H_
