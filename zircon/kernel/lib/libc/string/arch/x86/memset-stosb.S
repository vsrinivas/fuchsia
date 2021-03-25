// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/asm.h>

.text

// memset implementation relying on Intel's Enhanced REP STOSB optimization
// %rax = memset_stosb(%rdi, %rsi, %rdx)
.function memset_stosb, global
  // Save return value.
  mov %rdi, %r11

  mov %sil, %al
  mov %rdx, %rcx
  rep stosb // while (rcx-- > 0) *rdi++ = al;

  mov %r11, %rax
  ret
.end_function
