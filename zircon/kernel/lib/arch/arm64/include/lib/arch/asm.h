// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_ARM64_INCLUDE_LIB_ARCH_ASM_H_
#define ZIRCON_KERNEL_LIB_ARCH_ARM64_INCLUDE_LIB_ARCH_ASM_H_

// Get the generic file.
#include_next <lib/arch/asm.h>

#ifdef __ASSEMBLER__  // clang-format off

#ifndef __has_feature
#define __has_feature(x) 0
#endif

/// The kernel is compiled using -ffixed-x15 so the compiler will never use
/// this register.
percpu_ptr .req x15

/// This register is permanently reserved by the ABI in the compiler.
/// #if __has_feature(shadow_call_stack) it's used for the SCSP.
shadow_call_sp .req x18

/// Fill a register with a wide integer literal.
///
/// This emits the one to four instructions required to fill a 64-bit
/// register with a given bit pattern.  It uses as few instructions as
/// suffice for the particular value.
///
/// Parameters
///
///   * reg
///     - Required: Output 64-bit register.
///
///   * literal
///     - Required: An integer expression that can be evaluated immediately
///     without relocation.
///
.macro movlit reg, literal
mov \reg, #((\literal) & 0xffff)
.ifne (((\literal) >> 16) & 0xffff)
movk \reg, #(((\literal) >> 16) & 0xffff), lsl #16
.endif
.ifne (((\literal) >> 32) & 0xffff)
movk \reg, #(((\literal) >> 32) & 0xffff), lsl #32
.endif
.ifne (((\literal) >> 48) & 0xffff)
movk \reg, #(((\literal) >> 48) & 0xffff), lsl #48
.endif
.endm  // movlit

/// Materialize a symbol (with optional addend) into a register.
///
/// This emits the `adr` instruction or two-instruction sequence required
/// to materialize the address of a global variable or function symbol.
///
/// Parameters
///
///   * reg
///     - Required: Output 64-bit register.
///
///   * symbol
///     - Required: A symbolic expression requiring at most PC-relative reloc.
///
.macro adr_global reg, symbol
#if __has_feature(hwaddress_sanitizer)
  adrp \reg, :pg_hi21_nc:\symbol
  movk \reg, #:prel_g3:\symbol+0x100000000
  add \reg, \reg, #:lo12:\symbol
#elif defined(__AARCH64_CMODEL_TINY__)
  adr \reg, \symbol
#else
  adrp \reg, \symbol
  add \reg, \reg, #:lo12:\symbol
#endif
.endm  // adr_global

/// Load a 64-bit fixed global symbol (with optional addend) into a register.
///
/// This emits the `ldr` instruction or two-instruction sequence required to
/// load a global variable.  If multiple words are required, it's more
/// efficient to use `adr_global` and then `ldp` than to repeat `ldr_global`
/// with related locations.
///
/// Parameters
///
///   * reg
///     - Required: Output 64-bit register.
///
///   * symbol
///     - Required: A symbolic expression requiring at most PC-relative reloc.
///
.macro ldr_global reg, symbol
#ifdef __AARCH64_CMODEL_TINY__
  ldr \reg, \symbol
#else
  adrp \reg, \symbol
  ldr \reg, [\reg, #:lo12:\symbol]
#endif
.endm  // adr_global

#endif  // clang-format on

#endif  // ZIRCON_KERNEL_LIB_ARCH_ARM64_INCLUDE_LIB_ARCH_ASM_H_
