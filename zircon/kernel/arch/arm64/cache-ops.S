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

    // void arch_flush_cache_range(addr_t start, size_t len);
FUNCTION(arch_clean_cache_range)
    cache_range_op dc cvac arm64_dcache_size // clean cache to PoC by MVA
    ret
END_FUNCTION(arch_clean_cache_range)

    // void arch_flush_invalidate_cache_range(addr_t start, size_t len);
FUNCTION(arch_clean_invalidate_cache_range)
    cache_range_op dc civac arm64_dcache_size // clean & invalidate dcache to PoC by MVA
    ret
END_FUNCTION(arch_clean_invalidate_cache_range)

    // void arch_invalidate_cache_range(addr_t start, size_t len);
FUNCTION(arch_invalidate_cache_range)
    cache_range_op dc ivac arm64_dcache_size // invalidate dcache to PoC by MVA
    ret
END_FUNCTION(arch_invalidate_cache_range)

    // void arch_sync_cache_range(addr_t start, size_t len);
FUNCTION(arch_sync_cache_range)
    cache_range_op dc cvau arm64_dcache_size // clean dcache to PoU by MVA
    cache_range_op ic ivau arm64_icache_size // invalidate icache to PoU by MVA
    isb
    ret
END_FUNCTION(arch_sync_cache_range)

// Routine to iterate over all ways/sets across all levels of data caches
// from level 0 to the point of coherence.
//
// Adapted from example code in the ARM Architecture Reference Manual ARMv8.
.macro cache_way_set_op, op name
    mrs     x0, clidr_el1
    and     w3, w0, #0x07000000     // get 2x level of coherence
    lsr     w3, w3, #23
    cbz     w3, .Lfinished_\name
    mov     w10, #0                 // w10 = 2x cache level
    mov     w8, #1                  // w8 = constant 1
.Lloop1_\name:
    add     w2, w10, w10, lsr #1    // calculate 3x cache level
    lsr     w1, w0, w2              // extract 3 bit cache type for this level
    and     w1, w1, #0x7
    cmp     w1, #2
    b.lt    .Lskip_\name            // no data or unified cache at this level
    msr     csselr_el1, x10         // select this cache level
    isb                             // synchronize change to csselr
    mrs     x1, ccsidr_el1          // w1 = ccsidr
    and     w2, w1, #7              // w2 = log2(line len) - 4
    add     w2, w2, #4              // w2 = log2(line len)
    ubfx    w4, w1, #3, #10         // w4 = max way number, right aligned
    clz     w5, w4                  // w5 = 32 - log2(ways), bit position of way in DC operand
    lsl     w9, w4, w5              // w9 = max way number, aligned to position in DC operand
    lsl     w12, w8, w5             // w12 = amount to decrement way number per iteration

.Lloop2_\name:
    ubfx    w7, w1, #13, #15        // w7 = max set number, right aligned
    lsl     w7, w7, w2              // w7 = max set number, aligned to position in DC operand
    lsl     w13, w8, w2             // w13 = amount to decrement set number per iteration
.Lloop3_\name:
    orr     w11, w10, w9            // w11 = combine way number and cache number
    orr     w11, w11, w7            //       and set number for DC operand
    dc      \op, x11                // data cache op
    subs    w7, w7, w13             // decrement set number
    b.ge    .Lloop3_\name

    subs    x9, x9, x12             // decrement way number
    b.ge    .Lloop2_\name
.Lskip_\name:
    add     w10, w10, #2            // increment 2x cache level
    cmp     w3, w10
    dsb     sy                      // ensure completetion of previous cache maintainance instructions
    b.gt    .Lloop1_\name
.Lfinished_\name:
.endm

// Below are 3 variants of cache flushing routines by way/set for
// an individual cpu.

    // void arm64_local_invalidate_cache_all()
FUNCTION(arm64_local_invalidate_cache_all)
    cache_way_set_op isw, invalidate

    // dump the instruction cache as well
    ic      iallu
    isb

    ret
END_FUNCTION(arm64_local_invalidate_cache_all)

    // void arm64_local_clean_cache_all()
FUNCTION(arm64_local_clean_cache_all)
    cache_way_set_op csw, clean

    // dump the instruction cache as well
    ic      iallu
    isb

    ret
END_FUNCTION(arm64_local_clean_cache_all)

    // void arm64_local_clean_invalidate_cache_all()
FUNCTION(arm64_local_clean_invalidate_cache_all)
    cache_way_set_op cisw, clean_invalidate

    // dump the instruction cache as well
    ic      iallu
    isb

    ret
END_FUNCTION(arm64_local_clean_invalidate_cache_all)

