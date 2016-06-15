// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <asm.h>

/* x86 assembly macros used in a few files */

#define PHYS_LOAD_ADDRESS (MEMBASE + KERNEL_LOAD_OFFSET)
#define PHYS_ADDR_DELTA (KERNEL_BASE + KERNEL_LOAD_OFFSET - PHYS_LOAD_ADDRESS)
#define PHYS(x) ((x) - PHYS_ADDR_DELTA)

/* zeros the bss for the kernel */
/* trashes a lot of registers */
.macro bss_setup
    movl $PHYS(__bss_start), %edi /* starting address of the bss */
    movl $PHYS(__bss_end), %ecx   /* find the length of the bss in bytes */
    subl %edi, %ecx
    shrl $2, %ecx       /* convert to 32 bit words, since the bss is aligned anyway */
0:
    movl $0, (%edi)
    addl $4, %edi
    loop 0b
.endm

