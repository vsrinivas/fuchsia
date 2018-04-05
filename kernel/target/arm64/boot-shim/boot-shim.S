// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <arch/asm_macros.h>

// scratch register, not saved across function calls
tmp             .req x16

#define STACK_SIZE  4096

.section .text.boot0,"ax"
FUNCTION(_start)
    // x0 points to device tree at entry

    // compute beginning of kernel bootdata container
    adr     x1, _start
    add     x1, x1, #KERNEL_ALIGN

    // setup stack
    adr_global tmp, stack_end
    mov     sp, tmp

    // x0: pointer to device tree
    // x1: pointer to kernel bootdata container
    bl      boot_shim

    // kernel entry point is returned in x0
    mov     tmp, x0

    // pass bootdata to kernel in x0
    adr     x0, bootdata_return
    ldr     x0, [x0]

    br      tmp

END_FUNCTION(_start)

.data
DATA(bootdata_return)
    .balign 8
    .quad 0
END_DATA(bootdata_return)

.section .boot-shim.stack,"a",%nobits
LOCAL_DATA(stack)
    .balign 16
    .skip STACK_SIZE
LOCAL_DATA(stack_end)
END_DATA(stack)
