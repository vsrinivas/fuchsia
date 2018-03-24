// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/asm_macros.h>
#include <zircon/boot/bootdata.h>

// Register usage
bd_base     .req x9         // beginning of bootdata
bd_size     .req x10        // size of bootdata
kern_base   .req x11        // new location for kernel
src         .req x12        // source for copy
dest        .req x13        // dest for copy
count       .req x14        // counter for copy

// scratch registers
tmp         .req x15
tmp2        .req x16
wtmp        .req w15
wtmp2       .req w16

#define BOOTDATA_SIZE           32  // sizeof(bootdata_t)
#define BOOTDATA_TYPE_OFFSET    0
#define BOOTDATA_LENGTH_OFFSET  4

.section .text
_start:

    // fetch beginning of bootdata into bd_base
    adr     bd_base, _start
    add     bd_base, bd_base, #BOOT_SHIM_SIZE

    // sanity check the bootdata headers
    // the first bootdata record should have type BOOTDATA_CONTAINER
    // and the second should have type BOOTDATA_KERNEL
    ldr     wtmp, [bd_base, BOOTDATA_TYPE_OFFSET]
    movlit  wtmp2, BOOTDATA_CONTAINER
    cmp     wtmp, wtmp2
    bne     _fail
    ldr     wtmp, [bd_base, BOOTDATA_SIZE + BOOTDATA_TYPE_OFFSET]
    movlit  wtmp2, BOOTDATA_KERNEL
    cmp     wtmp, wtmp2
    bne     _fail

    // compute total boot data size
    ldr     wtmp, [bd_base, BOOTDATA_LENGTH_OFFSET] // read container length field
    ubfx    tmp, tmp, #0, #32                       // 64 bit extend
    add     bd_size, tmp, #BOOTDATA_SIZE            // add container header size

    // check to see if there are additional bootdata items following the kernel
    // if there are, we must relocate the kernel beyond the end of the bootdata
    // so the kernel's bss does not collide with it
    ldr     wtmp, [bd_base, BOOTDATA_SIZE + BOOTDATA_LENGTH_OFFSET]
    ubfx    tmp, tmp, #0, #32                       // 64 bit extend
    add     tmp, tmp, #(2 * BOOTDATA_SIZE)
    cmp     bd_size, tmp
    bne     _relocate_kernel

    // no bootdata following the kernel, so we can execute the kernel in place
    mov     kern_base, bd_base
    mov     x1, #0                                  // clear x1, since we have no bootdata to pass
    b       _jump_to_kernel

_relocate_kernel:
    // we have bootdata items following the kernel, so we will relocate the kernel
    // after the rest of the bootdata

    // if we have bootdata beyond the kernel, pass pointer to bootdata in x1
    // x0 will continue to point to device tree
    mov     x1, bd_base

    // compute new kernel location
    add     kern_base, bd_base, bd_size
    // round up to 64K byte boundary
    movlit  tmp, 0xffff
    add     kern_base, kern_base, tmp
    and     kern_base, kern_base, #0xffffffffffff0000

    // copy kernel to new location
    mov     src, bd_base
    mov     dest, kern_base

    // read kernel size from kernel bootdata header
    ldr     wtmp, [bd_base, BOOTDATA_SIZE + BOOTDATA_LENGTH_OFFSET]
    ubfx    count, tmp, #0, #32                     // 64 bit extend
    add     count, count, #(2 * BOOTDATA_SIZE)      // add size of the two bootdata headers
    add     count, count, #15                       // round up to 16 byte boundary
    and     count, count, #0xfffffffffffffff0

    // copy kernel image from src to dest, 16 bytes at a time
 _copy_loop:
    ldp     tmp, tmp2, [src], #16
    stp     tmp, tmp2, [dest], #16
    subs    count, count, #16
    bne     _copy_loop

_jump_to_kernel:
    add     tmp, kern_base, #(2 * BOOTDATA_SIZE)    // skip two bootdatas
    ldr     tmp, [tmp]                              // read the _start offset
    add     tmp, tmp, kern_base                     // add the offset
    br      tmp                                     // jump to it

_fail:
    b       _fail
