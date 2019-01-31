// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <lib/code_patching.h>

.text

// memcpy implementation relying on Intel's Enhanced REP MOVSB optimization
// %rax = memcpy_erms(%rdi, %rsi, %rdx)
FUNCTION(memcpy_erms)
    // Save return value.
    mov %rdi, %rax

    mov %rdx, %rcx
    rep movsb // while (rcx-- > 0) *rdi++ = *rsi++; /* rdi, rsi are uint8_t* */
    ret
END_FUNCTION(memcpy_erms)

// memcpy implementation that copies 8 bytes at a time when possible
// %rax = memcpy_quad(%rdi, %rsi, %rdx)
FUNCTION(memcpy_quad)
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
END_FUNCTION(memcpy_quad)

FUNCTION(memcpy)
    jmp memcpy_erms
    APPLY_CODE_PATCH_FUNC_WITH_DEFAULT(x86_memcpy_select, memcpy, 2)
END_FUNCTION(memcpy)
