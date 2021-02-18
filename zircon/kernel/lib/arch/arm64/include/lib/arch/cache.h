// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_ARM64_INCLUDE_LIB_ARCH_CACHE_H_
#define ZIRCON_KERNEL_LIB_ARCH_ARM64_INCLUDE_LIB_ARCH_CACHE_H_

#include <lib/arch/arm64/cache.h>
#include <lib/arch/intrin.h>

#include <cstddef>
#include <cstdint>

namespace arch {

// Ensures that the instruction and data caches are in coherence after the
// modification of provided address ranges. The caches are regarded as coherent
// - with respect to the ranges passed to SyncRange() - only after the
// associated object is destroyed.
class CacheConsistencyContext {
 public:
  // Constructs a CacheConsistencyContext with an expectation around whether
  // virtual address aliasing is possible among the address ranges to be
  // recorded.
  explicit CacheConsistencyContext(bool possible_aliasing)
      : possible_aliasing_(possible_aliasing) {}

  // Defaults to the general assumption that aliasing among the address ranges
  // to be recorded is possible if the instruction cache is VIPT.
  CacheConsistencyContext() = default;

  // Ensures consistency on destruction.
  ~CacheConsistencyContext();

  // Records a virtual address range that should factor into consistency.
  void SyncRange(uintptr_t vaddr, size_t size);

 private:
  const bool possible_aliasing_ = CacheTypeEl0::Read().l1_ip() == ArmL1ICachePolicy::VIPT;
};

}  // namespace arch
#endif  // ZIRCON_KERNEL_LIB_ARCH_ARM64_INCLUDE_LIB_ARCH_CACHE_H_
