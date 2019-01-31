// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "asm.h"

// Call the C _dl_start, which returns a dl_start_return_t containing the
// user entry point and its argument.  Then jump to that entry point with
// the argument in the first argument register, pushing a zero return
// address and clearing the frame pointer register so the user entry point
// is the base of the call stack.

.hidden _start
ENTRY(_start)
    // We can be pretty sure that we were started with the stack pointer
    // correctly aligned, which is (rsp % 16) = 8 at function entry.
    // Since we'd need to adjust down by 8 to make an immediate call with
    // correct stack alignment, it's just as cheap to explicitly align and
    // then we're resilient to process setup not having given us the
    // ABI-required alignment, just in case.
    and $-16,%rsp
    xor %rbp,%rbp

    call _dl_start
    mov %rax,%rdi
    push %rbp                   // Zero in the return address slot.
    jmp *%rdx
END(_start)
