// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014, Google Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#include <asm.h>
#include <arch/ops.h>
#include <arch/defines.h>
#include <arch/arm64/cache_loop.h>

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

