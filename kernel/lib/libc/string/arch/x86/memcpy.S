// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>

.text

// %rax = memcpy(%rdi, %rsi, %rdx)
FUNCTION(memcpy)
    // Save return value.
    mov %rdi, %rax

    mov %rdx, %rcx
    rep movsb // while (rcx-- > 0) *rdi++ = *rsi++;

.Lret:
    ret
END_FUNCTION(memcpy)
