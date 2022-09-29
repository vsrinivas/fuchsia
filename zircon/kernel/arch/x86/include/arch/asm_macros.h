// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_ASM_MACROS_H_
#define ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_ASM_MACROS_H_

// clang-format off

// Use these when pushing a register from the calling frame.
// Here we can easily describe where to find the value of the register.

.macro push_reg reg
pushq \reg
.cfi_adjust_cfa_offset 8
.cfi_rel_offset \reg, 0
.endm

.macro pop_reg reg
popq \reg
.cfi_adjust_cfa_offset -8
.cfi_same_value \reg
.endm

// Use these when pushing a value other than the above, or when pushing
// a register that is not from the calling frame.
// Here we just punt on trying to describe the value pushed, the caller
// can do so if desired.

.macro push_value value
pushq \value
.cfi_adjust_cfa_offset 8
.endm

.macro pop_value value
popq \value
.cfi_adjust_cfa_offset -8
.endm

// Use these when adding/subtracting values from the stack pointer.
// The value must be positive.

.macro sub_from_sp value
sub $\value, %rsp
.cfi_adjust_cfa_offset \value
.endm

.macro add_to_sp value
add $\value, %rsp
.cfi_adjust_cfa_offset -\value
.endm

// For "functions" that are not normal functions in the ABI sense.
// Treat all previous frame registers as having the same value.

#define ALL_CFI_SAME_VALUE \
  .cfi_same_value %rax ; \
  .cfi_same_value %rbx ; \
  .cfi_same_value %rcx ; \
  .cfi_same_value %rdx ; \
  .cfi_same_value %rsi ; \
  .cfi_same_value %rdi ; \
  .cfi_same_value %rbp ; \
  .cfi_same_value %r8  ; \
  .cfi_same_value %r9  ; \
  .cfi_same_value %r10 ; \
  .cfi_same_value %r11 ; \
  .cfi_same_value %r12 ; \
  .cfi_same_value %r13 ; \
  .cfi_same_value %r14 ; \
  .cfi_same_value %r15

// Treat all previous frame registers as not being restorable.

#define ALL_CFI_UNDEFINED \
  .cfi_undefined %rax ; \
  .cfi_undefined %rbx ; \
  .cfi_undefined %rcx ; \
  .cfi_undefined %rdx ; \
  .cfi_undefined %rsi ; \
  .cfi_undefined %rdi ; \
  .cfi_undefined %rbp ; \
  .cfi_undefined %r8  ; \
  .cfi_undefined %r9  ; \
  .cfi_undefined %r10 ; \
  .cfi_undefined %r11 ; \
  .cfi_undefined %r12 ; \
  .cfi_undefined %r13 ; \
  .cfi_undefined %r14 ; \
  .cfi_undefined %r15

// RET with a dispatch-serializing instruction afterwards. Prevents processors
// from speculatively executing instructions after the RET. Some processors
// may speculatively execute instructions after RET without this fence.
#define RET_AND_SPECULATION_POSTFENCE \
  ret; \
  int3

// Unconditional JMP with a dispatch-serializing instruction afterwards.
// Prevents processors from speculatively executing instructions after the JMP.
// Some processors may speculatively execute instructions after RET without this
// fence.
#define JMP_AND_SPECULATION_POSTFENCE(_x) \
  jmp _x; \
  int3

#endif  // ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_ASM_MACROS_H_
