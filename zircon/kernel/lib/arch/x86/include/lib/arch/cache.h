// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_X86_INCLUDE_LIB_ARCH_CACHE_H_
#define ZIRCON_KERNEL_LIB_ARCH_X86_INCLUDE_LIB_ARCH_CACHE_H_

#include <cpuid.h>

#include <cstddef>
#include <cstdint>

#include "intrin.h"

namespace arch {

// Ensures that the instruction and data caches are in coherence after the
// modification of provided address ranges. The caches are regarded as coherent
// - with respect to the ranges passed to SyncRange() - only after the
// associated object is destroyed.
struct GlobalCacheConsistencyContext {
  // Ensures consistency on destruction.
  ~GlobalCacheConsistencyContext() {
    // [amd/vol2]: 7.6.1  Cache Organization and Operation.
    // AMD documents that the processor will do its own checking and flushing of
    // the instruction cache, so that software need not take any action.
    //
    // [intel/vol3]: 8.1.3  Handling Self- and Cross-Modifying Code.
    // Intel recommends executing a serializing instruction after any self- or
    // cross-modification.
    SerializeInstructions();
  }

  // Records a virtual address range that should factor into consistency.
  void SyncRange(uintptr_t vaddr, size_t size) {
    // No action required. See comment above.
  }
};

}  // namespace arch
#endif  // ZIRCON_KERNEL_LIB_ARCH_X86_INCLUDE_LIB_ARCH_CACHE_H_
