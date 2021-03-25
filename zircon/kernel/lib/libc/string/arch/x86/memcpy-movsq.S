// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/asm.h>

.text

// memcpy implementation that copies 8 bytes at a time when possible
// %rax = memcpy_movsq(%rdi, %rsi, %rdx)
.function memcpy_movsq, global
  // Save return value.
  mov %rdi, %rax

  // Copy all of the 8 byte chunks we can
  mov %rdx, %rcx
  shr $3, %rcx
  rep movsq // while (rcx-- > 0) { *rdi++ = *rsi++; /* rdi, rsi are uint64_t* */ }

  // Copy the rest
  mov %rdx, %rcx
  and $0x7, %rcx
  rep movsb
  ret
.end_function
