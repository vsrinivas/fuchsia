// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <lib/code_patching.h>

.text

// memset implementation relying on Intel's Enhanced REP STOSB optimization
// %rax = memset(%rdi, %rsi, %rdx)
FUNCTION(memset_erms)
    // Save return value.
    mov %rdi, %r11

    mov %sil, %al
    mov %rdx, %rcx
    rep stosb // while (rcx-- > 0) *rdi++ = al;

    mov %r11, %rax
    ret
END_FUNCTION(memset_erms)

// memset implementation that sets 8 bytes at a time when possible
// %rax = memset_quad(%rdi, %rsi, %rdx)
FUNCTION(memset_quad)
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
END_FUNCTION(memset_quad)

FUNCTION(memset)
    jmp memset_erms
    APPLY_CODE_PATCH_FUNC_WITH_DEFAULT(x86_memset_select, memset, 2)
END_FUNCTION(memset)
