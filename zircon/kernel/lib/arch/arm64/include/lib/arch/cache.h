// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_ARM64_INCLUDE_LIB_ARCH_CACHE_H_
#define ZIRCON_KERNEL_LIB_ARCH_ARM64_INCLUDE_LIB_ARCH_CACHE_H_

#include <lib/arch/arm64/cache.h>
#include <lib/arch/internal/cache_loop.h>
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
  bool possible_aliasing_ = CacheTypeEl0::Read().l1_ip() == ArmL1ICachePolicy::VIPT;
};

// Invalidate the entire instruction cache.
//
// Caller must perform an instruction barrier (e.g., `__isb(ARM_MB_SY)`)
// prior to relying on the operation being complete.
inline void InvalidateGlobalInstructionCache() {
  // Instruction cache: invalidate all ("iall") inner-sharable ("is") caches
  // to point of unification ("u").
  asm volatile("ic ialluis" ::: "memory");
}

// Invalidate both the instruction and data TLBs.
//
// Caller must perform an instruction barrier (e.g., `__isb(ARM_MB_SY)`)
// prior to relying on the operation being complete.
inline void InvalidateLocalTlbs() { asm volatile("tlbi vmalle1" ::: "memory"); }

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

// Generate assembly to iterate over all ways/sets across all levels of data
// caches from level 0 to the point of coherence.
//
// "op" should be an ARM64 operation that is called on each set/way, such as
// "csw" (i.e., "Clean by Set and Way").
//
// Generated assembly does not use the stack, but clobbers registers [x0 -- x13].
.macro data_cache_way_set_op op, name
  data_cache_way_set_op_impl \op, \name
.endm

// clang-format on

#endif  // __ASSEMBLER__

#endif  // ZIRCON_KERNEL_LIB_ARCH_ARM64_INCLUDE_LIB_ARCH_CACHE_H_
