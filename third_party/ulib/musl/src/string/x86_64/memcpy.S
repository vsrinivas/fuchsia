// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "asm.h"

// %rax = memcpy(%rdi, %rsi, %rdx)
.hidden __memcpy_fwd
ENTRY(memcpy)
ALIAS_ENTRY(__memcpy_fwd)
ALIAS_ENTRY(__unsanitized_memcpy)
ASAN_ALIAS_ENTRY(memcpy)

    // Save return value.
    mov %rdi, %rax

    mov %rdx, %rcx
    rep movsb // while (rcx-- > 0) *rdi++ = *rsi++;

    ret

ALIAS_END(__unsanitized_memcpy)
ASAN_ALIAS_END(memcpy)
ALIAS_END(__memcpy_fwd)
END(memcpy)
