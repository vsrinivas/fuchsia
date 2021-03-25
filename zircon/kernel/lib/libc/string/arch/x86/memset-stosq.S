// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/asm.h>

.text

// memset implementation that sets 8 bytes at a time when possible
// %rax = memset_stosq(%rdi, %rsi, %rdx)
.function memset_stosq, global
  // Save return value.
  mov %rdi, %r11

  // Create an 8-byte copy of the pattern
  mov %rdx, %rcx
  movzx %sil, %rax
  movq $0x0101010101010101, %r10
  mul %r10
  mov %rcx, %rdx

  // Copy all of the 8 byte chunks we can
  shr $3, %rcx
  rep stosq // while (rcx-- > 0) { *rdi++ = rax; /* rdi is uint64_t* */ }

  // Copy the rest
  mov %rdx, %rcx
  and $0x7, %rcx
  rep stosb

  mov %r11, %rax
  ret
.end_function
