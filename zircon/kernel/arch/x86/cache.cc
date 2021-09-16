// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <align.h>
#include <lib/arch/x86/boot-cpuid.h>

#include <arch/ops.h>
#include <arch/x86.h>
#include <arch/x86/feature.h>
#include <fbl/function.h>

uint32_t arch_dcache_line_size(void) {
  return arch::BootCpuid<arch::CpuidProcessorInfo>().cache_line_size_bytes();
}

uint32_t arch_icache_line_size(void) {
  return arch::BootCpuid<arch::CpuidProcessorInfo>().cache_line_size_bytes();
}

void arch_sync_cache_range(vaddr_t start, size_t len) {
  // Invoke cpuid to act as a serializing instruction.  This will ensure we
  // see modifications to future parts of the instruction stream.  See
  // Intel Volume 3, 8.1.3 "Handling Self- and Cross-Modifying Code".  cpuid
  // is the more conservative approach suggested in this section.
  uint32_t v;
  cpuid(0, &v, &v, &v, &v);
}

void arch_invalidate_cache_range(vaddr_t start, size_t len) {}

static void for_each_cacheline(vaddr_t start, size_t len, fbl::Function<void(vaddr_t)> function) {
  const size_t clsize = arch::BootCpuid<arch::CpuidProcessorInfo>().cache_line_size_bytes();
  vaddr_t end = start + len;
  vaddr_t ptr = ROUNDDOWN(start, clsize);
  while (ptr < end) {
    function(ptr);
    ptr += clsize;
  }
}

void arch_clean_cache_range(vaddr_t start, size_t len) {
  if (likely(x86_feature_test(X86_FEATURE_CLWB))) {
    for_each_cacheline(start, len, +[](vaddr_t ptr) { __asm__ volatile("clwb %0" ::"m"(*(char*)ptr)); });
    __asm__ volatile("sfence");
  } else {
    arch_clean_invalidate_cache_range(start, len);
  }
}

void arch_clean_invalidate_cache_range(vaddr_t start, size_t len) {
  if (unlikely(!x86_feature_test(X86_FEATURE_CLFLUSH))) {
    __asm__ volatile("wbinvd");
    return;
  }

  if (likely(x86_feature_test(X86_FEATURE_CLFLUSHOPT))) {
    for_each_cacheline(start, len, +[](vaddr_t ptr) { __asm__ volatile("clflushopt %0" ::"m"(*(char*)ptr)); });
    __asm__ volatile("sfence");
  } else {
    for_each_cacheline(start, len, +[](vaddr_t ptr) { __asm__ volatile("clflush %0" ::"m"(*(char*)ptr)); });
  }
}
