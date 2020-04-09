// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_ASM_H_
#define ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_ASM_H_

// This may be overridden by a $current_cpu/include/lib/arch/asm.h
// file that does #include_next to get this one before adding more.

#include <lib/arch/internal/asm.h>

#ifdef __ASSEMBLER__  // clang-format off

/// Defines an ELF symbol at the current assembly position, with specified
/// scope and (optional) type.
///
/// Parameters
///
///   * name
///     - Required: Symbol name to define.
///
///   * scope
///     - Optional: One of these strings:
///       - `local`: The symbol is visible only within this assembly file.
///       - `global`: The symbol is visible throughout this linked module.
///       - `export`: The symbol is exported for dynamic linking (user mode).
///     - Default: `local`
///
///   * type
///     - Optional: ELF symbol type.  This only has practical effect when
///     dynamic linking is involved, but it's convention to set it consistently
///     to `function` or `object` for named entities with an st_size field,
///     and leave it the default `notype` only for labels within an entity.
///     - Default: `notype`
///
/// This is only really useful when the scope and/or type is set to a
/// non-default value.  `.label name, local` is just `name:`.
.macro .label name, scope=local, type=notype
  // Set ELF symbol type.
  .type \name, %\type

  // Set ELF symbol visibility and binding, which represent scope.
  .ifnc \scope, local
    .globl \name
    .ifnc \scope, export
      .hidden \name
    .else
      .ifnc \scope, global
	.error "`scope` argument `\scope` not `local`, `global`, or `export`"
      .endif
    .endif
  .endif

  // Define the label itself.
  \name\():
.endm  // .label

/// Define a function that extends until `.end`.
///
/// Parameters
///
///   * name
///     - Required: Symbol name to define.
///
///   * scope
///     - Optional: See `.label`.
///     - Default: `local`
///
///   * cfi
///     - Optional: One of the strings:
///       - `abi`: This is a function with the standard C++ ABI.
///       - `custom`: This function includes `.cfi_*` directives that
///       describe its unwinding requirements completely.
///       - `none`: Don't emit normal function CFI for this function.
///     - Default: `abi`
///
///   * align
///     - Optional: Minimum byte alignment for this function's code.
///     - Default: none
///
///   * nosection
///     - Optional: Must be exactly `nosection` to indicate this function goes
///     into the assembly's current section rather than a per-function section.
///
.macro .function name, scope=local, cfi=abi, align=, nosection=
  // Validate the \cfi argument.  The valid values correspond to
  // the `_.function.cfi.{start,end}.\cfi` subroutine macros.
  .ifnc foo, foo
    .error "fmh"
  .endif
  .ifnc \cfi, abi
    .ifnc \cfi, custom
      .ifnc \cfi, none
        .error "`cfi` argument `\cfi` not `abi`, `custom`, or `none`"
      .endif
    .endif
  .endif
  _.entity \name, \scope, \align, \nosection, function, function, _.function.end.\cfi
  _.function.start.\cfi
.endm  // .function

/// Define a data object that extends until `.end_object`.
///
/// This starts the definition of a data object and is matched by
/// `.end_object` to finish that object's definition.  `.end_object` must
/// appear before any other `.object` or `.function` directive.
///
/// Parameters
///
///   * name
///     - Required: Symbol name to define.
///
///   * type
///     - Optional: One of the strings:
///       - `bss`: Define a zero-initialized (.bss) writable data object.
///       This is usually followed by just a `.skip` directive and then
///       `.end_object`.
///       - `data`: Define an initialized writable data object.  This is
///       followed by data-emitting directives (`.int` et al) to provide
///       the initializer, and then `.end_object`.
///       - `relro`: Define a read-only initialized data object requiring
///       dynamic relocation.  Use this instead of `rodata` if initializer
///       data includes any absolute address constants.
///       - `rodata`: Define a pure read-only initialized data object.
///     - Default: `data`
///
///   * scope
///     - Optional: See `.label`.
///     - Default: `local`
///
///   * align
///     - Optional: Minimum byte alignment for this function's code.
///     - Default: none
///
///   * nosection
///     - Optional: Must be exactly `nosection` to indicate this object goes
///     into the assembly's current section rather than a per-object section.
///
.macro .object name, type=data, scope=local, align=, nosection=
  .ifnc \type, bss
    .ifnc \type, data
      .ifnc \type, relro
        .ifnc \type, rodata
          .error "`type` argument `\type` not `bss`, `data, `relro`, or `rodata`"
        .endif
      .endif
    .endif
  .endif
  _.entity \name, \scope, \align, \nosection, object, \type
.endm  // .start_object

#endif  // clang-format on

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_ASM_H_
