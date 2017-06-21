// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>

.text

// %rax = memset(%rdi, %rsi, %rdx)
FUNCTION(memset)
    // Save return value.
    mov %rdi, %r11

    mov %sil, %al
    mov %rdx, %rcx
    rep stosb // while (rcx-- > 0) *rdi++ = al;

    mov %r11, %rax
    ret
END_FUNCTION(memset)
