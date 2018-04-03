// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <arch/asm_macros.h>

// scratch registers, not saved across function calls
tmp             .req x16
tmp2            .req x17

// call-saved registers
device_tree     .req x19
bootdata        .req x20

#define STACK_SIZE  4096

.section .text.boot0,"ax"
FUNCTION(_start)
    // x0 points to device tree at entry
    mov     device_tree, x0

    // compute beginning of bootdata
    adr     x1, _start
    add     x1, x1, #KERNEL_ALIGN
    mov     bootdata, x1

    // setup stack
    adr_global tmp, stack_end
    mov     sp, tmp

    // x0: pointer to device tree
    // x1: pointer to bootdata
    bl      boot_shim
    // kernel entry point is returned in x0

    mov     tmp, x0
    // pass device tree and bootdata to kernel in x0 and x1
    mov     x0, device_tree

    // TODO(voydanoff) remove the "no_relocate" after we remove device tree support from the kernel
    // did we relocate the kernel?
    add     tmp2, bootdata, #(KERNEL_ALIGN * 2)
    cmp     tmp, tmp2
    blt     no_relocate

    // pass bootdata in x1 if we relocated the kernel
    mov     x1, bootdata
    br      tmp

no_relocate:
    // if we are just a kernel image then we have no bootdata to pass
    mov     x1, #0
    br      tmp

END_FUNCTION(_start)

.section .boot-shim.stack,"a",%nobits
LOCAL_DATA(stack)
    .balign 16
    .skip STACK_SIZE
LOCAL_DATA(stack_end)
END_DATA(stack)
