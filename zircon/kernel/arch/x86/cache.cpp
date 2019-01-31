// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/ops.h>
#include <arch/x86.h>
#include <arch/x86/feature.h>

uint32_t arch_dcache_line_size(void) {
    return x86_get_clflush_line_size();
}

uint32_t arch_icache_line_size(void) {
    return x86_get_clflush_line_size();
}

void arch_sync_cache_range(addr_t start, size_t len) {
    // Invoke cpuid to act as a serializing instruction.  This will ensure we
    // see modifications to future parts of the instruction stream.  See
    // Intel Volume 3, 8.1.3 "Handling Self- and Cross-Modifying Code".  cpuid
    // is the more conservative approach suggested in this section.
    uint32_t v;
    cpuid(0, &v, &v, &v, &v);
}

void arch_invalidate_cache_range(addr_t start, size_t len) {
}

void arch_clean_cache_range(addr_t start, size_t len) {
    // TODO: consider wiring up clwb if present
    arch_clean_invalidate_cache_range(start, len);
}

void arch_clean_invalidate_cache_range(addr_t start, size_t len) {
    if (unlikely(!x86_feature_test(X86_FEATURE_CLFLUSH))) {
        __asm__ volatile("wbinvd");
        return;
    }

    // clflush/clflushopt is present
    const vaddr_t clsize = x86_get_clflush_line_size();
    addr_t end = start + len;
    addr_t ptr = ROUNDDOWN(start, clsize);

    // TODO: use run time patching to merge these two paths
    if (likely(x86_feature_test(X86_FEATURE_CLFLUSHOPT))) {
        while (ptr < end) {
            __asm__ volatile("clflushopt %0" ::"m"(*(char*)ptr));
            ptr += clsize;
        }
    } else {
        while (ptr < end) {
            __asm__ volatile("clflush %0" ::"m"(*(char*)ptr));
            ptr += clsize;
        }
    }

    __asm__ volatile("mfence");
}
