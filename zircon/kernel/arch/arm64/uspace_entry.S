// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <arch/arm64/iframe.h>
#include <arch/asm_macros.h>

// void arm64_uspace_entry(iframe_t* iframe, vaddr_t kstack) __NO_RETURN;
FUNCTION(arm64_uspace_entry)
    mov sp, x1
    ldr x2, [x0, ARM64_IFRAME_OFFSET_USP]
    msr sp_el0, x2
    ldr x2, [x0, ARM64_IFRAME_OFFSET_ELR]
    msr elr_el1, x2
    ldr x2, [x0, ARM64_IFRAME_OFFSET_SPSR]
    msr spsr_el1, x2
    ldr x2, [x0, ARM64_IFRAME_OFFSET_MDSCR]
    msr mdscr_el1, x2

    // Setting x0 deferred to last, we're still using it.
    ldp x1, x2, [x0, ARM64_IFRAME_OFFSET_R + (1 * 8)]
    ldp x3, x4, [x0, ARM64_IFRAME_OFFSET_R + (3 * 8)]
    ldp x5, x6, [x0, ARM64_IFRAME_OFFSET_R + (5 * 8)]
    ldp x7, x8, [x0, ARM64_IFRAME_OFFSET_R + (7 * 8)]
    ldp x9, x10, [x0, ARM64_IFRAME_OFFSET_R + (9 * 8)]
    ldp x11, x12, [x0, ARM64_IFRAME_OFFSET_R + (11 * 8)]
    ldp x13, x14, [x0, ARM64_IFRAME_OFFSET_R + (13 * 8)]
    ldp x15, x16, [x0, ARM64_IFRAME_OFFSET_R + (15 * 8)]
    ldp x17, x18, [x0, ARM64_IFRAME_OFFSET_R + (17 * 8)]
    ldp x19, x20, [x0, ARM64_IFRAME_OFFSET_R + (19 * 8)]
    ldp x21, x22, [x0, ARM64_IFRAME_OFFSET_R + (21 * 8)]
    ldp x23, x24, [x0, ARM64_IFRAME_OFFSET_R + (23 * 8)]
    ldp x25, x26, [x0, ARM64_IFRAME_OFFSET_R + (25 * 8)]
    ldp x27, x28, [x0, ARM64_IFRAME_OFFSET_R + (27 * 8)]
    ldp x29, x30, [x0, ARM64_IFRAME_OFFSET_R + (29 * 8)]
    ldr x0, [x0, ARM64_IFRAME_OFFSET_R + (0 * 8)]

    // Lazy loading of the FPU means we don't need to zero the simd registers
    eret
    SPECULATION_POSTFENCE
END_FUNCTION(arm64_uspace_entry)
