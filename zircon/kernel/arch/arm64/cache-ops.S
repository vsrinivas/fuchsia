// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014, Google Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#include <asm.h>
#include <arch/ops.h>
#include <arch/defines.h>

.text

.macro cache_range_op, cache op size_var
    adrp    x16, \size_var
    ldr     w4, [x16, :lo12:\size_var]  // cache line size in bytes

    add     x2, x0, x1                  // calculate the end address

    sub     x5, x4, #1                  // cache line size mask
    bic     x3, x0, x5                  // cache align the start address by applying inverse mask

.Lcache_range_op_loop\@:
    \cache  \op, x3
    add     x3, x3, x4
    cmp     x3, x2
    blo     .Lcache_range_op_loop\@
    dsb     sy
.endm

    /* void arch_flush_cache_range(addr_t start, size_t len); */
FUNCTION(arch_clean_cache_range)
    cache_range_op dc cvac arm64_dcache_size // clean cache to PoC by MVA
    ret
END_FUNCTION(arch_clean_cache_range)

    /* void arch_flush_invalidate_cache_range(addr_t start, size_t len); */
FUNCTION(arch_clean_invalidate_cache_range)
    cache_range_op dc civac arm64_dcache_size // clean & invalidate dcache to PoC by MVA
    ret
END_FUNCTION(arch_clean_invalidate_cache_range)

    /* void arch_invalidate_cache_range(addr_t start, size_t len); */
FUNCTION(arch_invalidate_cache_range)
    cache_range_op dc ivac arm64_dcache_size // invalidate dcache to PoC by MVA
    ret
END_FUNCTION(arch_invalidate_cache_range)

    /* void arch_sync_cache_range(addr_t start, size_t len); */
FUNCTION(arch_sync_cache_range)
    cache_range_op dc cvau arm64_dcache_size // clean dcache to PoU by MVA
    cache_range_op ic ivau arm64_icache_size // invalidate icache to PoU by MVA
    isb
    ret
END_FUNCTION(arch_sync_cache_range)

/* void arch_invalidate_cache_all()
 *      should only be used early in boot, prior to enabling mmu/cache
 */
FUNCTION(arch_invalidate_cache_all)
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
    dc      isw, x11
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
    ret
END_FUNCTION(arch_invalidate_cache_all)

