// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "asm.h"

// %rax = memset(%rdi, %rsi, %rdx)
ENTRY(memset)
    // Save return value.
    mov %rdi, %r11

    mov %sil, %al
    mov %rdx, %rcx
    rep stosb // while (rcx-- > 0) *rdi++ = al;

    mov %r11, %rax
    ret
END(memset)

ALIAS(memset, __unsanitized_memset)
ASAN_WEAK_ALIAS(memset)
