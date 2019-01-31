// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

// clang-format off

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
.endm

// GAS doesn't support xzr in .cfi directives.

.macro push_regs ra, rb
stp \ra, \rb, [sp,#-16]!
.cfi_adjust_cfa_offset 16
.ifnes "\ra", "xzr"
.cfi_rel_offset \ra, 0
.endif
.ifnes "\rb", "xzr"
.cfi_rel_offset \rb, 8
.endif
.endm

.macro pop_regs ra, rb
ldp \ra, \rb, [sp], #16
.cfi_adjust_cfa_offset -16
.ifnes "\ra", "xzr"
.cfi_same_value \ra
.endif
.ifnes "\rb", "xzr"
.cfi_same_value \rb
.endif
.endm

.macro sub_from_sp value
sub sp, sp, #\value
.cfi_adjust_cfa_offset \value
.endm

.macro add_to_sp value
add sp, sp, #\value
.cfi_adjust_cfa_offset -\value
.endm

.macro adr_global reg, symbol
adrp \reg, \symbol
add \reg, \reg, #:lo12:\symbol
.endm

.macro movabs reg, symbol
// TODO(mcgrathr): Remove this workaround when the upstream LLVM assembler
// bug http://bugs.llvm.org/show_bug.cgi?id=32527 is fixed.
#ifdef __clang__
ldr \reg, =\symbol
#else
movz \reg, #:abs_g0_nc:\symbol
movk \reg, #:abs_g1_nc:\symbol
movk \reg, #:abs_g2_nc:\symbol
movk \reg, #:abs_g3:\symbol
#endif
.endm

.macro tbzmask, reg, mask, label, shift=0
.if \shift >= 64
    .error "tbzmask: unsupported mask, \mask"
.elseif \mask == 1 << \shift
    tbz     \reg, #\shift, \label
.else
    tbzmask \reg, \mask, \label, "(\shift + 1)"
.endif
.endm

.macro tbnzmask, reg, mask, label, shift=0
.if \shift >= 64
    .error "tbnzmask: unsupported mask, \mask"
.elseif \mask == 1 << \shift
    tbnz     \reg, #\shift, \label
.else
    tbnzmask \reg, \mask, \label, "(\shift + 1)"
.endif
.endm

// For "functions" that are not normal functions in the ABI sense.
// Treat all previous frame registers as having the same value.

#define ALL_CFI_SAME_VALUE \
    .cfi_same_value x0 ; \
    .cfi_same_value x1 ; \
    .cfi_same_value x2 ; \
    .cfi_same_value x3 ; \
    .cfi_same_value x4 ; \
    .cfi_same_value x5 ; \
    .cfi_same_value x6 ; \
    .cfi_same_value x7 ; \
    .cfi_same_value x8 ; \
    .cfi_same_value x9 ; \
    .cfi_same_value x10 ; \
    .cfi_same_value x11 ; \
    .cfi_same_value x12 ; \
    .cfi_same_value x13 ; \
    .cfi_same_value x14 ; \
    .cfi_same_value x15 ; \
    .cfi_same_value x16 ; \
    .cfi_same_value x17 ; \
    .cfi_same_value x18 ; \
    .cfi_same_value x19 ; \
    .cfi_same_value x20 ; \
    .cfi_same_value x21 ; \
    .cfi_same_value x22 ; \
    .cfi_same_value x23 ; \
    .cfi_same_value x24 ; \
    .cfi_same_value x25 ; \
    .cfi_same_value x26 ; \
    .cfi_same_value x27 ; \
    .cfi_same_value x28 ; \
    .cfi_same_value x29
