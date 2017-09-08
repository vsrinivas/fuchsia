// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/ops.h>
#include <arch/x86.h>

void arch_sync_cache_range(addr_t start, size_t len)
{
    // Invoke cpuid to act as a serializing instruction.  This will ensure we
    // see modifications to future parts of the instruction stream.  See
    // Intel Volume 3, 8.1.3 "Handling Self- and Cross-Modifying Code".  cpuid
    // is the more conservative approach suggested in this section.
    uint32_t v;
    cpuid(0, &v, &v, &v, &v);
}

void arch_invalidate_cache_range(addr_t start, size_t len)
{
}

void arch_clean_cache_range(addr_t start, size_t len)
{
    __asm__ volatile("wbinvd");
}

void arch_clean_invalidate_cache_range(addr_t start, size_t len)
{
    __asm__ volatile("wbinvd");
}
