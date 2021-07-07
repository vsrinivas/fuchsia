// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/cache.h>

namespace arch {

GlobalCacheConsistencyContext::~GlobalCacheConsistencyContext() {
  // If CTR_EL0.DIC is unset, invalidating the instruction cache to the PoU
  // is required for data-to-instruction cache coherence. Furthermore, if there
  // is possible aliasing then we cannot rely on invalidation by virtual
  // address and must resort to invalidating the entirety of the instruction
  // cache.
  if (!CacheTypeEl0::Read().dic() && possible_aliasing_) {
    InvalidateGlobalInstructionCache();
    __isb(ARM_MB_SY);
  }
}

void GlobalCacheConsistencyContext::SyncRange(uintptr_t vaddr, size_t size) {
  const auto ctr = CacheTypeEl0::Read();
  // If CTR_EL0.IDC is unset, cleaning the data cache to the PoU is required
  // for instruction-to-data cache coherence.
  if (!ctr.idc()) {
    const size_t dcache_line_size = ctr.dcache_line_size();
    // Ensure alignment with the data cache line size (a power of two).
    const uintptr_t aligned_addr = vaddr & ~(dcache_line_size - 1);
    const uintptr_t end_addr = aligned_addr + (vaddr - aligned_addr) + size;
    for (uintptr_t line = aligned_addr; line < end_addr; line += dcache_line_size) {
      __asm__ volatile("dc cvau, %0" ::"r"(line) : "memory");
    }
    __dsb(ARM_MB_ISH);
  }

  // A continuation of the comment on a similar block in the destructor. If
  // CTR_EL0.DIC is unset, then we must invalidate - and if there is not
  // aliasing, then we can rely on invalidation by virtual address.
  if (!ctr.dic() && !possible_aliasing_) {
    const size_t icache_line_size = ctr.icache_line_size();
    // Ensure alignment with the data instruction line size (a power of two).
    const uintptr_t aligned_addr = vaddr & ~(icache_line_size - 1);
    const uintptr_t end_addr = aligned_addr + (vaddr - aligned_addr) + size;
    for (uintptr_t line = aligned_addr; line < end_addr; line += icache_line_size) {
      __asm__ volatile("ic ivau, %0" ::"r"(line) : "memory");
    }
    __isb(ARM_MB_SY);
  }
}

}  // namespace arch
