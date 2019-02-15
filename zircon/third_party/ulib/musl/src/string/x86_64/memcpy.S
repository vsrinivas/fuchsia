// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "asm.h"

// %rax = memcpy(%rdi, %rsi, %rdx)
ENTRY(memcpy)
    // Save return value.
    mov %rdi, %rax

    mov %rdx, %rcx
    rep movsb // while (rcx-- > 0) *rdi++ = *rsi++;

    ret
END(memcpy)

#ifdef HIDDEN
    .hidden memcpy
#else
ALIAS(memcpy, __unsanitized_memcpy)
ASAN_WEAK_ALIAS(memcpy)
#endif
ALIAS(memcpy, __memcpy_fwd)
.hidden __memcpy_fwd
