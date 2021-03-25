// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/asm.h>

.text

// %rax = memcpy_movsb(%rdi, %rsi, %rdx)
.function memcpy_movsb, global
  // Save return value.
  mov %rdi, %rax

  mov %rdx, %rcx
  rep movsb // while (rcx-- > 0) *rdi++ = *rsi++; /* rdi, rsi are uint8_t* */
  ret
.end_function
