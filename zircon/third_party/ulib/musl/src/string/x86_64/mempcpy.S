// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "asm.h"

// %rax = mempcpy(%rdi, %rsi, %rdx)
ENTRY(mempcpy)

    mov %rdx, %rcx
    rep movsb // while (rcx-- > 0) *rdi++ = *rsi++;

    mov %rdi, %rax
    ret

END(mempcpy)
