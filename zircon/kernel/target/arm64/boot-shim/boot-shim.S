// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <arch/asm_macros.h>
#include <zircon/boot/image.h>

#define SCTLR_I     (1 << 12)   // Instruction cache enable
#define SCTLR_C     (1 << 2)    // Cache enable
#define SCTLR_M     (1 << 0)    // MMU enable

// scratch register, not saved across function calls
tmp             .req x16

#define STACK_SIZE  4096

.section .text.boot0,"ax"
FUNCTION(_start)
    // magic instruction that gives us UEFI "MZ" signature
    add x13, x18, #0x16
    b header_end

    .quad   0             // image offset from start of ram (unused)
    .quad   0             // image size (unused)
    .quad   0
    .quad   0
    .quad   0
    .quad   0

    // arm64 magic number
    .byte   'A'
    .byte   'R'
    .byte   'M'
    .byte   0x64
    .align 3

header_end:
    // x0 typically points to device tree at entry

    // what EL are we running at?
    mrs     tmp, CurrentEL
    cmp     tmp, #(1 << 2)
    beq     cache_disable_el1

    // Disable caches and MMU (EL2 version)
    mrs     tmp, sctlr_el2
    bic     tmp, tmp, #SCTLR_I
    bic     tmp, tmp, #SCTLR_C
    bic     tmp, tmp, #SCTLR_M
    msr     sctlr_el2, tmp
    b       cache_disable_done

cache_disable_el1:
    // Disable caches and MMU (EL1 version)
    mrs     tmp, sctlr_el1
    bic     tmp, tmp, #SCTLR_I
    bic     tmp, tmp, #SCTLR_C
    bic     tmp, tmp, #SCTLR_M
    msr     sctlr_el1, tmp

cache_disable_done:
    dsb     sy
    isb

    // setup stack
    adr     tmp, stack_end
    mov     sp, tmp

    // x0: pointer to device tree
    bl      boot_shim

    // x0: bootdata_t* to pass to kernel
    // x1: kernel entry point
    br      x1
END_FUNCTION(_start)

.bss
.balign 16
LOCAL_DATA(stack)
    .skip STACK_SIZE
LOCAL_DATA(stack_end)
END_DATA(stack)
