 // Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <arch/asm_macros.h>
#include <arch/defines.h>

tmp                     .req x9
cpuid                   .req x12
spintable               .req x13

FUNCTION(bcm28xx_park_cpu)
// x0 is current cpuid
// x1 is the address of the spin table

    // Save the arguments because we're about to trash x0 and x1
    mov cpuid, x0
    mov spintable, x1

    // Disable the caches and the MMU
    mrs tmp, sctlr_el1
    bic tmp, tmp, #(1<<0)    /* Disable MMU */
    bic tmp, tmp, #(1<<12)   /* Disable instruction cache */
    bic tmp, tmp, #(1<<2)    /* Disable data cache */
    msr sctlr_el1, tmp

    // Write zero into the spin table for the current core because the spin loop
    // is programmed to spin until this vector is set to something other than
    // 0.
    str xzr, [x13, x12, lsl #3]

    // Clean-invalidate all levels of the cache for this core.
    // This ensures that we don't have cache entries lying around in the core
    // after it has been shut down.
    // The original implementation can be found in the ARMv8-A TRM or at the
    // following URL: http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.den0024a/BABJDBHI.html
        mrs     x0, clidr_el1
        and     w3, w0, #0x07000000
        lsr     w3, w3, #23
        cbz     w3, finished
        mov     w10, #0
        mov     w8, #1
    loop1:
        add     w2, w10, w10, lsr #1
        lsr     w1, w0, w2
        and     w1, w1, #0x7
        cmp     w1, #2
        b.lt    skip
        msr     csselr_el1, x10
        isb
        mrs     x1, ccsidr_el1
        and     w2, w1, #7
        add     w2, w2, #4
        ubfx    w4, w1, #3, #10
        clz     w5, w4
        lsl     w9, w4, w5

        lsl     w16, w8, w5

    loop2:
        ubfx    w7, w1, #13, #15
        lsl     w7, w7, w2
        lsl     w17, w8, w2
    loop3:
        orr     w11, w10, w9
        orr     w11, w11, w7
        dc      cisw, x11
        subs    w7, w7, w17
        b.ge    loop3

        subs    x9, x9, x16
        b.ge    loop2
    skip:
        add     w10, w10, #2
        cmp     w3, w10
        dsb     sy
        b.gt    loop1
    finished:
        ic      iallu
        isb
        dsb sy

// Spin until somebody sets our CPU jump vector and signals us via a send event
// (SEV) instruction.
secondary_spin:
    wfe
    ldr tmp, [spintable, cpuid, lsl #3]
    cbz tmp, secondary_spin

    // Seconary CPUs shouldn't really care what we pass as arguments, but we
    // zero them out anyway.
    mov x0, 0
    mov x1, 0
    mov x2, 0
    mov x3, 0

    // Follow the new cpu vector.
    br  tmp

END_FUNCTION(bcm28xx_park_cpu)

/* This .ltorg emits any immediate constants here. We need to put this before
 * the bcm28xx_park_cpu_end symbol because we intend to relocate the assembly
 * contained within the mexec_asm[_end] block. Any constants needed by this
 * block should also be relocated so we need to ensure that they occur before
 * bcm28xx_park_cpu_end.
 */
.ltorg

DATA(bcm28xx_park_cpu_end)
