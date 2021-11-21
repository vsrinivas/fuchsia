// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_RISCV64_INCLUDE_LIB_ARCH_CACHE_H_
#define ZIRCON_KERNEL_LIB_ARCH_RISCV64_INCLUDE_LIB_ARCH_CACHE_H_

#include <lib/arch/riscv64/cache.h>
#include <lib/arch/intrin.h>

#ifndef __ASSEMBLER__

#include <cstddef>
#include <cstdint>

namespace arch {

// Ensures that the instruction and data caches are in coherence after the
// modification of provided address ranges. The caches are regarded as coherent
// - with respect to the ranges passed to SyncRange() - only after the
// associated object is destroyed.
class GlobalCacheConsistencyContext {
 public:
  // Constructs a GlobalCacheConsistencyContext with an expectation around whether
  // virtual address aliasing is possible among the address ranges to be
  // recorded.
  explicit GlobalCacheConsistencyContext(bool possible_aliasing)
      : possible_aliasing_(possible_aliasing) {}

  // Defaults to the general assumption that aliasing among the address ranges
  // to be recorded is possible if the instruction cache is VIPT.
  GlobalCacheConsistencyContext() = default;

  // Ensures consistency on destruction.
  ~GlobalCacheConsistencyContext();

  // Records a virtual address range that should factor into consistency.
  void SyncRange(uintptr_t vaddr, size_t size);

 private:
  const bool possible_aliasing_ = true;
};

// Invalidate the entire instruction cache.
inline void InvalidateGlobalInstructionCache() {
}

// Invalidate both the instruction and data TLBs.
inline void InvalidateLocalTlbs() {  }

// Local per-cpu cache flush routines.
//
// These clean or invalidate the data and instruction caches from the point
// of view of a single CPU to the point of coherence.
//
// These are typically only useful during system setup or shutdown when
// the MMU is not enabled. Other use-cases should use range-based cache operation.
extern "C" void CleanLocalCaches();
extern "C" void InvalidateLocalCaches();
extern "C" void CleanAndInvalidateLocalCaches();

// Disables the local caches and MMU, ensuring that the former are flushed
// (along with the TLB).
extern "C" void DisableLocalCachesAndMmu();

}  // namespace arch

#else  // __ASSEMBLER__

// clang-format off


// clang-format on

#endif  // __ASSEMBLER__

#endif  // ZIRCON_KERNEL_LIB_ARCH_RISCV64_INCLUDE_LIB_ARCH_CACHE_H_
