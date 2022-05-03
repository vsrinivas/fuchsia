// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/asm.h>

.text

// EFI uses different argument registers in the C calling convention.
#ifdef _WIN32
#define ARG0 %rcx
#else
#define ARG0 %rdi
#endif

// void LoadCodeSegmentSelector(uint16_t code_segment);
//
// Load the %cs segment selector with `code_segment`.
//
// This is hard to implement with inline assembly because the "lretq"
// instruction (used to perform a far jump, which in turn is required to
// update %cs) requires us to push/pop data to/from the stack. GCC/Clang don't
// give inline assembly freedom to modify the stack pointer (and attempting to
// do so can break compiler red zone optimisations, where the compiler performs
// reads/writes beyond the stack pointer).
//
.function LoadCodeSegmentSelector, global

  // Set %cs to `code_segment`.
  //
  // We do this by doing a far return operation (which pops the new %rip and
  // new %cs off the stack) to the label "1".
  leaq   1f(%rip), %rax
  pushq  ARG0  // `code_segment`
  .cfi_adjust_cfa_offset 8
  pushq  %rax  // new PC
  .cfi_adjust_cfa_offset 8
  lretq
  .cfi_adjust_cfa_offset -16
1:
  retq

.end_function
